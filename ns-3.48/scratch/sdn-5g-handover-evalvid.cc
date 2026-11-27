#include "ns3/ofswitch13-module.h"
#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/evalvid-client-server-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-module.h"
#include "ns3/point-to-point-module.h"

#include <iomanip>
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Sdn5gHandoverEvalvid");

// Callbacks para rastrear eventos de conexao e Handover X2 no RRC do UE
void
NotifyConnectionEstablishedUe(std::string context,
                                uint64_t imsi,
                                uint16_t cellId,
                                uint16_t rnti)
{
    std::cout << "[" << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
              << "s] UE RRC Connection Established | IMSI: " << imsi << " | CellId: " << cellId
              << " | RNTI: " << rnti << std::endl;
}

void
NotifyHandoverStartUe(std::string context,
                      uint64_t imsi,
                      uint16_t cellId,
                      uint16_t rnti,
                      uint16_t targetCellId)
{
    std::cout << "[" << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
              << "s] >>> HANDOVER START (X2) | IMSI: " << imsi << " | Source CellId: " << cellId
              << " -> Target CellId: " << targetCellId << " | RNTI: " << rnti << std::endl;
}

void
NotifyHandoverEndOkUe(std::string context,
                      uint64_t imsi,
                      uint16_t cellId,
                      uint16_t rnti)
{
    std::cout << "[" << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
              << "s] <<< HANDOVER END OK (X2) | IMSI: " << imsi << " | New CellId: " << cellId
              << " | New RNTI: " << rnti << std::endl;
}

int
main(int argc, char* argv[])
{
    // Parametros configuraveis via linha de comando
    double simTime = 12.0;           // Tempo total de simulacao (s)
    double ueSpeed = 25.0;           // Velocidade do UE (m/s) -> cruza a borda entre gNBs
    double centralFrequency = 3.5e9; // Banda n78 (3.5 GHz)
    double bandwidth = 20e6;         // 20 MHz
    uint16_t numerology = 1;         // SCS = 30 kHz
    double txPower = 35.0;           // Potencia de transmissao gNB (dBm)
    std::string traceFile = "st_akiyo_cif.st";
    bool enableOfsLogs = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Tempo total de simulacao em segundos", simTime);
    cmd.AddValue("ueSpeed", "Velocidade do UE em m/s", ueSpeed);
    cmd.AddValue("centralFrequency", "Frequencia central 5G NR em Hz", centralFrequency);
    cmd.AddValue("bandwidth", "Largura de banda 5G NR em Hz", bandwidth);
    cmd.AddValue("numerology", "Numerologia 5G NR (0, 1, 2)", numerology);
    cmd.AddValue("traceFile", "Arquivo de trace de video EvalVid (.st)", traceFile);
    cmd.AddValue("enableOfsLogs", "Habilita logs detalhados do OFSwitch13", enableOfsLogs);
    cmd.Parse(argc, argv);

    if (enableOfsLogs)
    {
        OFSwitch13Helper::EnableDatapathLogs();
    }

    // Configuracoes globais para suporte a full-duplex nos enlaces CSMA do SDN

    // =========================================================================
    // 1. CRIACAO DOS NOS DA REDE (gNBs, UE, Switches SDN, Controlador e Host)
    // =========================================================================
    NodeContainer gnbNodes;
    gnbNodes.Create(2);

    NodeContainer ueNodes;
    ueNodes.Create(1);

    NodeContainer switchNodes;
    switchNodes.Create(2); // Switch 1 (Edge PGW) e Switch 2 (Core/Server)

    Ptr<Node> controllerNode = CreateObject<Node>();
    Ptr<Node> remoteHost = CreateObject<Node>();

    // =========================================================================
    // 2. MOBILIDADE (2 gNBs fixas a 200m de distancia + 1 UE cruzando entre elas)
    // =========================================================================
    MobilityHelper mobility;

    Ptr<ListPositionAllocator> gnbPosAlloc = CreateObject<ListPositionAllocator>();
    gnbPosAlloc->Add(Vector(0.0, 0.0, 15.0));   // gNB 0 (CellId 1)
    gnbPosAlloc->Add(Vector(200.0, 0.0, 15.0)); // gNB 1 (CellId 2)
    mobility.SetPositionAllocator(gnbPosAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(gnbNodes);

    // UE inicia proximo a gNB 0 e se desloca em direcao a gNB 1
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    ueMobility.Install(ueNodes);
    Ptr<ConstantVelocityMobilityModel> ueVel =
        ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    ueVel->SetPosition(Vector(20.0, 20.0, 1.5));
    ueVel->SetVelocity(Vector(ueSpeed, 0.0, 0.0));

    // =========================================================================
    // 3. CONFIGURACAO DO 5G-LENA v5.1 (NR + EPC + HANDOVER X2)
    // =========================================================================
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    Ptr<IdealBeamformingHelper> idealBeamformingHelper = CreateObject<IdealBeamformingHelper>();
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();

    nrHelper->SetBeamformingHelper(idealBeamformingHelper);
    nrHelper->SetEpcHelper(epcHelper);

    // Configura algoritmo de Handover A3-RSRP
    nrHelper->SetHandoverAlgorithmType("ns3::NrA3RsrpHandoverAlgorithm");
    nrHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(1.5));
    nrHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(128)));

    // Configuracao de espectro e canal 3GPP UMa
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigureFactories("UMa", "LOS", "ThreeGpp");

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(centralFrequency, bandwidth, 1);
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    // Antenas e potencia
    nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(txPower));
    nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(numerology));
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));

    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(1));
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));

    NetDeviceContainer gnbNetDev = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueNetDev = nrHelper->InstallUeDevice(ueNodes, allBwps);

    // Cria interface X2 entre gNB 0 e gNB 1 para suportar Handover
    nrHelper->AddX2Interface(gnbNodes);

    // =========================================================================
    // 4. BACKHAUL SDN OPENFLOW 1.3 (PGW <-> Switch 1 <-> Switch 2 <-> RemoteHost)
    // =========================================================================
    Ptr<Node> pgw = epcHelper->GetPgwNode();

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
    csma.SetDeviceAttribute("Mtu", UintegerValue(1500));

    NetDeviceContainer switchPorts[2];
    NetDeviceContainer hostDevices; // [0] = PGW, [1] = RemoteHost

    // Enlace 1: PGW <-> Switch 1 (Porta 1 do Switch 1)
    NetDeviceContainer linkPgwSw1 = csma.Install(NodeContainer(pgw, switchNodes.Get(0)));
    hostDevices.Add(linkPgwSw1.Get(0));
    switchPorts[0].Add(linkPgwSw1.Get(1));

    // Enlace 2: Switch 1 (Porta 2) <-> Switch 2 (Porta 1)
    NetDeviceContainer linkSw1Sw2 =
        csma.Install(NodeContainer(switchNodes.Get(0), switchNodes.Get(1)));
    switchPorts[0].Add(linkSw1Sw2.Get(0));
    switchPorts[1].Add(linkSw1Sw2.Get(1));

    // Enlace 3: Switch 2 (Porta 2) <-> RemoteHost
    NetDeviceContainer linkSw2Host = csma.Install(NodeContainer(switchNodes.Get(1), remoteHost));
    switchPorts[1].Add(linkSw2Host.Get(0));
    hostDevices.Add(linkSw2Host.Get(1));

    // Instala o controlador SDN e os switches OpenFlow 1.3
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    of13Helper->InstallController(controllerNode);
    of13Helper->InstallSwitch(switchNodes.Get(0), switchPorts[0]);
    of13Helper->InstallSwitch(switchNodes.Get(1), switchPorts[1]);
    of13Helper->CreateOpenFlowChannels();

    // =========================================================================
    // 5. PILHA TCP/IP, ENDERECAMENTO E ROTEAMENTO
    // =========================================================================
    InternetStackHelper internet;
    internet.Install(remoteHost);
    internet.Install(ueNodes);

    // Rede IP do Backhaul SDN (PGW = 10.1.1.1, RemoteHost = 10.1.1.2)
    Ipv4AddressHelper ipv4Sdn;
    ipv4Sdn.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer sdnIfaces = ipv4Sdn.Assign(hostDevices);
    Ipv4Address pgwSdnIp = sdnIfaces.GetAddress(0);
    Ipv4Address remoteHostIp = sdnIfaces.GetAddress(1);

    // Rota no RemoteHost para alcancar a sub-rede 5G dos UEs (7.0.0.0/8) via PGW
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                               Ipv4Mask("255.0.0.0"),
                                               pgwSdnIp,
                                               1);

    // Atribui IP 5G ao UE e configura gateway padrao
    Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueNetDev));
    Ipv4Address ueIp = ueIpIface.GetAddress(0);

    Ptr<Ipv4StaticRouting> ueStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(0)->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);

    // Conecta o UE inicialmente a gNB mais proxima (gNB 0)
    nrHelper->AttachToClosestGnb(ueNetDev, gnbNetDev);

    // =========================================================================
    // 6. FLUXOS DE QOS 5G E APLICACOES EVALVID (DOWNLINK E UPLINK)
    // =========================================================================
    uint16_t dlPort = 4000;
    uint16_t ulPort = 5000;

    // QoS Flow dedicado para Video Downlink (RemoteHost -> UE)
    NrQosFlow dlVideoFlow(NrQosFlow::GBR_CONV_VIDEO);
    Ptr<NrQosRule> dlVideoRule = Create<NrQosRule>();
    NrQosRule::PacketFilter dlPf;
    dlPf.localPortStart = dlPort;
    dlPf.localPortEnd = dlPort;
    dlVideoRule->Add(dlPf);
    nrHelper->ActivateDedicatedQosFlow(ueNetDev.Get(0), dlVideoFlow, dlVideoRule);

    // QoS Flow dedicado para Video Uplink (UE -> RemoteHost)
    NrQosFlow ulVideoFlow(NrQosFlow::GBR_CONV_VIDEO);
    Ptr<NrQosRule> ulVideoRule = Create<NrQosRule>();
    NrQosRule::PacketFilter ulPf;
    ulPf.remotePortStart = ulPort;
    ulPf.remotePortEnd = ulPort;
    ulVideoRule->Add(ulPf);
    nrHelper->ActivateDedicatedQosFlow(ueNetDev.Get(0), ulVideoFlow, ulVideoRule);

    // --- STREAMING DOWNLINK (Servidor no RemoteHost -> Cliente no UE) ---
    EvalvidServerHelper dlServer(dlPort);
    dlServer.SetAttribute("SenderTraceFilename", StringValue(traceFile));
    dlServer.SetAttribute("SenderDumpFilename", StringValue("sd_dl_video"));
    dlServer.SetAttribute("PacketPayload", UintegerValue(1014));
    ApplicationContainer dlServerApp = dlServer.Install(remoteHost);
    dlServerApp.Start(Seconds(1.0));
    dlServerApp.Stop(Seconds(simTime - 0.5));

    EvalvidClientHelper dlClient(remoteHostIp, dlPort);
    dlClient.SetAttribute("ReceiverDumpFilename", StringValue("rd_dl_video"));
    ApplicationContainer dlClientApp = dlClient.Install(ueNodes.Get(0));
    dlClientApp.Start(Seconds(1.1));
    dlClientApp.Stop(Seconds(simTime - 0.2));

    // --- STREAMING UPLINK (Servidor no UE -> Cliente no RemoteHost) ---
    EvalvidServerHelper ulServer(ulPort);
    ulServer.SetAttribute("SenderTraceFilename", StringValue(traceFile));
    ulServer.SetAttribute("SenderDumpFilename", StringValue("sd_ul_video"));
    ulServer.SetAttribute("PacketPayload", UintegerValue(1014));
    ApplicationContainer ulServerApp = ulServer.Install(ueNodes.Get(0));
    ulServerApp.Start(Seconds(1.2));
    ulServerApp.Stop(Seconds(simTime - 0.5));

    EvalvidClientHelper ulClient(ueIp, ulPort);
    ulClient.SetAttribute("ReceiverDumpFilename", StringValue("rd_ul_video"));
    ApplicationContainer ulClientApp = ulClient.Install(remoteHost);
    ulClientApp.Start(Seconds(1.3));
    ulClientApp.Stop(Seconds(simTime - 0.2));

    // =========================================================================
    // 7. TRACES DE HANDOVER E FLOWMONITOR
    // =========================================================================
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/ConnectionEstablished",
                    MakeCallback(&NotifyConnectionEstablishedUe));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStartUe));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOkUe));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    std::cout << "=== Iniciando Simulacao 5G Handover + SDN (OFSwitch13) + EvalVid (UL/DL) ==="
              << std::endl;
    std::cout << "UE IP: " << ueIp << " | RemoteHost IP: " << remoteHostIp
              << " | PGW SDN IP: " << pgwSdnIp << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // =========================================================================
    // 8. RELATORIO DE METRICAS DE REDE (FLOWMONITOR)
    // =========================================================================
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    std::cout << "\n=== Resultados FlowMonitor (Downlink e Uplink) ===" << std::endl;
    for (const auto& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        std::string direction = (t.sourceAddress == remoteHostIp) ? "DOWNLINK (Video)"
                                : (t.sourceAddress == ueIp)       ? "UPLINK (Video)"
                                                                  : "OUTRO";

        double duration =
            (flow.second.timeLastRxPacket - flow.second.timeFirstTxPacket).GetSeconds();
        double throughputMbps =
            (duration > 0.0) ? (flow.second.rxBytes * 8.0 / duration / 1e6) : 0.0;
        double meanDelayMs = (flow.second.rxPackets > 0)
                                 ? (flow.second.delaySum.GetSeconds() * 1000.0 / flow.second.rxPackets)
                                 : 0.0;
        double lossRate =
            (flow.second.txPackets > 0)
                ? (100.0 * (flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets)
                : 0.0;

        std::cout << "Flow " << flow.first << " [" << direction << "] (" << t.sourceAddress << ":"
                  << t.sourcePort << " -> " << t.destinationAddress << ":" << t.destinationPort
                  << ")\n"
                  << "  Tx Packets: " << flow.second.txPackets
                  << " | Rx Packets: " << flow.second.rxPackets
                  << " | Perda: " << std::fixed << std::setprecision(2) << lossRate << "%\n"
                  << "  Throughput: " << std::setprecision(3) << throughputMbps << " Mbps"
                  << " | Atraso Medio: " << std::setprecision(2) << meanDelayMs << " ms\n";
    }

    std::cout << "\nTraces EvalVid gerados: sd_dl_video, rd_dl_video, sd_ul_video, rd_ul_video"
              << std::endl;

    Simulator::Destroy();
    return 0;
}

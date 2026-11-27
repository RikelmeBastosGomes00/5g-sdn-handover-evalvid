/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// 1. OFSwitch13 deve vir antes da STL para evitar colisão de struct list no C++23
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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Sdn5gHandoverPbrH264");

static uint32_t g_handoverCount = 0;

void
NotifyHandoverStart(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCellId)
{
    std::cout << "[HANDOVER-START] t=" << Simulator::Now().GetSeconds() << "s IMSI=" << imsi
              << " Cell=" << cellId << " -> TargetCell=" << targetCellId << std::endl;
}

void
NotifyHandoverEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    g_handoverCount++;
    std::cout << "[HANDOVER-OK #" << g_handoverCount << "] t=" << Simulator::Now().GetSeconds()
              << "s IMSI=" << imsi << " NovaCell=" << cellId << " RNTI=" << rnti << std::endl;
}

int
main(int argc, char* argv[])
{
    uint32_t nUes = 10;
    bool enablePbr = true;
    double simTime = 12.0;
    std::string traceFile = "st_akiyo_cif.st";

    CommandLine cmd(__FILE__);
    cmd.AddValue("nUes", "Numero total de UEs na rede (1..20)", nUes);
    cmd.AddValue("enablePbr", "Ativa SDN Policy-Based Routing (limiar qDelay > 100ms)", enablePbr);
    cmd.AddValue("simTime", "Tempo total de simulacao (s)", simTime);
    cmd.AddValue("traceFile", "Arquivo de trace RTP do EvalVid", traceFile);
    cmd.Parse(argc, argv);

    if (nUes < 1)
    {
        nUes = 1;
    }

    // Configuracao deterministica do canal 3GPP (UMa / LOS)
    Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(0)));
    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(500 * 1024));

    // 1. Criacao dos nos: 3 gNBs (multiplos handovers X2), nUes e RemoteHost
    NodeContainer gnbNodes;
    gnbNodes.Create(3);

    NodeContainer ueNodes;
    ueNodes.Create(nUes);

    Ptr<Node> remoteHost = CreateObject<Node>();
    InternetStackHelper internet;
    internet.Install(remoteHost);

    // 2. Configuracao 5G-LENA (NR SA) + EPC + Handover X2
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    Ptr<IdealBeamformingHelper> beamformingHelper = CreateObject<IdealBeamformingHelper>();
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();

    nrHelper->SetBeamformingHelper(beamformingHelper);
    nrHelper->SetEpcHelper(epcHelper);

    // Banda de operacao 3.5 GHz (n78), 40 MHz
    Ptr<NrChannelHelper> channelHelper = CreateObject<NrChannelHelper>();
    channelHelper->ConfigureFactories("UMa", "LOS", "ThreeGpp");

    CcBwpCreator ccBwpCreator;
    CcBwpCreator::SimpleOperationBandConf bandConf(3.5e9, 40e6, 1);
    OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    channelHelper->AssignChannelsToBands({band});
    BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps({band});

    // Multiplas Antenas (MIMO Array: 8 elementos [2x4] nas gNBs e 4 elementos [2x2] nos UEs)
    nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(4));
    nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));

    nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(2));
    nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));

    // Algoritmo de Handover A3 (RSRP)
    nrHelper->SetHandoverAlgorithmType("ns3::NrA3RsrpHandoverAlgorithm");
    nrHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(1.5));
    nrHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(64)));

    // 3. Mobilidade: 3 gNBs em linha + UE 0 cruzando todas as celulas + UEs concorrentes
    Ptr<ListPositionAllocator> gnbPos = CreateObject<ListPositionAllocator>();
    gnbPos->Add(Vector(150.0, 0.0, 25.0));
    gnbPos->Add(Vector(450.0, 0.0, 25.0));
    gnbPos->Add(Vector(750.0, 0.0, 25.0));

    MobilityHelper gnbMobility;
    gnbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    gnbMobility.SetPositionAllocator(gnbPos);
    gnbMobility.Install(gnbNodes);

    // UE 0: percorre de x=50m ate x=830m em 12s (~65 m/s) cruzando gNB1 -> gNB2 -> gNB3
    MobilityHelper ue0Mobility;
    ue0Mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    ue0Mobility.Install(ueNodes.Get(0));
    ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetPosition(Vector(50.0, 15.0, 1.5));
    ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(Vector(65.0, 0.0, 0.0));

    // UEs 1 .. nUes-1: distribuidos nas 3 celulas para gerar carga concorrente
    for (uint32_t i = 1; i < nUes; ++i)
    {
        MobilityHelper bgMobility;
        bgMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        bgMobility.Install(ueNodes.Get(i));
        double xPos = 130.0 + (i % 6) * 120.0;
        double yPos = (i % 2 == 0) ? 35.0 : -35.0;
        ueNodes.Get(i)->GetObject<ConstantPositionMobilityModel>()->SetPosition(
            Vector(xPos, yPos, 1.5));
    }

    NetDeviceContainer gnbNetDev = nrHelper->InstallGnbDevice(gnbNodes, allBwps);
    NetDeviceContainer ueNetDev = nrHelper->InstallUeDevice(ueNodes, allBwps);

    // Interface X2 entre as 3 gNBs para suportar multiplos handovers
    nrHelper->AddX2Interface(gnbNodes);

    // Pilha IP nos UEs e associacao inicial a gNB mais proxima
    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueNetDev));
    nrHelper->AttachToClosestGnb(ueNetDev, gnbNetDev);

    // 4. Backhaul SDN (OFSwitch13) entre RemoteHost e PGW com Rota Principal vs Rota PBR
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NodeContainer sdnSwitches;
    sdnSwitches.Create(2); // Switch 0 (Ingresso RemoteHost) e Switch 1 (Egresso PGW)
    Ptr<Node> ofControllerNode = CreateObject<Node>();

    CsmaHelper csmaLink;
    // Rota padrao congestionavel (18 Mbps, fila 35 pacotes -> estoura sob carga multi-UE)
    csmaLink.SetChannelAttribute("DataRate", DataRateValue(DataRate("18Mbps")));
    csmaLink.SetChannelAttribute("Delay", TimeValue(MilliSeconds(5)));

    NetDeviceContainer hostToSw0 = csmaLink.Install(NodeContainer(remoteHost, sdnSwitches.Get(0)));
    NetDeviceContainer sw0ToSw1Primary =
        csmaLink.Install(NodeContainer(sdnSwitches.Get(0), sdnSwitches.Get(1)));
    NetDeviceContainer sw1ToPgw = csmaLink.Install(NodeContainer(sdnSwitches.Get(1), pgw));

    // Caminho alternativo de baixo atraso (PBR: ativado quando qDelay > 100ms)
    CsmaHelper csmaAltPbr;
    csmaAltPbr.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csmaAltPbr.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
    NetDeviceContainer sw0ToSw1Alt =
        csmaAltPbr.Install(NodeContainer(sdnSwitches.Get(0), sdnSwitches.Get(1)));

    // Configura fila limitada na porta primaria do Switch 0 para reproduzir congestionamento
    Ptr<CsmaNetDevice> primaryPortDev = DynamicCast<CsmaNetDevice>(sw0ToSw1Primary.Get(0));
    primaryPortDev->GetQueue()->SetMaxSize(QueueSize("35p"));

    // Controlador OpenFlow 1.3 (Learning/PBR)
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    Ptr<OFSwitch13LearningController> ofCtrl = CreateObject<OFSwitch13LearningController>();
    of13Helper->InstallController(ofControllerNode, ofCtrl);

    NetDeviceContainer sw0Ports;
    sw0Ports.Add(hostToSw0.Get(1));
    sw0Ports.Add(enablePbr ? sw0ToSw1Alt.Get(0) : sw0ToSw1Primary.Get(0));

    NetDeviceContainer sw1Ports;
    sw1Ports.Add(enablePbr ? sw0ToSw1Alt.Get(1) : sw0ToSw1Primary.Get(1));
    sw1Ports.Add(sw1ToPgw.Get(0));

    of13Helper->InstallSwitch(sdnSwitches.Get(0), sw0Ports);
    of13Helper->InstallSwitch(sdnSwitches.Get(1), sw1Ports);
    of13Helper->CreateOpenFlowChannels();

    // Enderecamento IP do backhaul SDN
    Ipv4AddressHelper ipv4Backhaul;
    ipv4Backhaul.SetBase("10.1.1.0", "255.255.255.0");
    NetDeviceContainer edgeDevs;
    edgeDevs.Add(hostToSw0.Get(0));
    edgeDevs.Add(sw1ToPgw.Get(1));
    Ipv4InterfaceContainer backhaulIfaces = ipv4Backhaul.Assign(edgeDevs);

    Ipv4Address remoteHostAddr = backhaulIfaces.GetAddress(0);
    Ipv4Address pgwAddr = backhaulIfaces.GetAddress(1);

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                               Ipv4Mask("255.0.0.0"),
                                               pgwAddr,
                                               1);

    Ptr<Ipv4StaticRouting> pgwStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(pgw->GetObject<Ipv4>());
    pgwStaticRouting->AddNetworkRouteTo(Ipv4Address("10.1.1.0"),
                                        Ipv4Mask("255.255.255.0"),
                                        remoteHostAddr,
                                        1);

    // 5. Aplicacao EvalVid SOMENTE DOWNLINK (RemoteHost -> UE 0, porta UDP 4000)
    uint16_t dlVideoPort = 4000;
    Ipv4Address ue0Addr = ueIpIface.GetAddress(0);

    EvalvidServerHelper videoServer(dlVideoPort);
    videoServer.SetAttribute("SenderTraceFilename", StringValue(traceFile));
    videoServer.SetAttribute("SenderDumpFilename", StringValue("sd_dl_video"));
    videoServer.SetAttribute("PacketPayload", UintegerValue(1024));
    ApplicationContainer serverApp = videoServer.Install(remoteHost);
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(simTime - 0.5));

    EvalvidClientHelper videoClient(remoteHostAddr, dlVideoPort);
    videoClient.SetAttribute("ReceiverDumpFilename", StringValue("rd_dl_video"));
    ApplicationContainer clientApp = videoClient.Install(ueNodes.Get(0));
    clientApp.Start(Seconds(0.5));
    clientApp.Stop(Seconds(simTime));

    // 6. Trafego concorrente UDP Downlink nos UEs 1 .. nUes-1 (2.5 Mbps por usuario)
    for (uint32_t i = 1; i < nUes; ++i)
    {
        Ipv4Address bgUeAddr = ueIpIface.GetAddress(i);
        uint16_t bgPort = 6000 + i;

        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), bgPort));
        sinkHelper.Install(ueNodes.Get(i)).Start(Seconds(0.5));

        OnOffHelper onOff("ns3::UdpSocketFactory", InetSocketAddress(bgUeAddr, bgPort));
        onOff.SetConstantRate(DataRate("2.5Mbps"), 1200);
        onOff.Install(remoteHost).Start(Seconds(0.8));
    }

    // Rastreamento de eventos de Handover X2
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStart));
    Config::Connect("/NodeList/*/DeviceList/*/NrUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOk));

    // Monitoramento de fluxo (FlowMonitor)
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    auto stats = monitor->GetFlowStats();

    double dlMbps = 0.0;
    double dlDelayMs = 0.0;
    double dlJitterMs = 0.0;
    double dlLossPct = 0.0;
    uint32_t txPkts = 0;
    uint32_t rxPkts = 0;

    for (const auto& kv : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        if (t.destinationPort == dlVideoPort)
        {
            txPkts = kv.second.txPackets;
            rxPkts = kv.second.rxPackets;
            if (rxPkts > 0)
            {
                double duration =
                    (kv.second.timeLastRxPacket - kv.second.timeFirstTxPacket).GetSeconds();
                dlMbps = (duration > 0.0) ? (kv.second.rxBytes * 8.0 / duration / 1e6) : 0.0;
                dlDelayMs = (kv.second.delaySum.GetSeconds() / rxPkts) * 1000.0;
                dlJitterMs = (rxPkts > 1)
                                 ? (kv.second.jitterSum.GetSeconds() / (rxPkts - 1)) * 1000.0
                                 : 0.0;
            }
            dlLossPct = (txPkts > 0) ? ((double)(txPkts - rxPkts) / txPkts) * 100.0 : 0.0;
        }
    }

    std::cout << "\n==================== RESUMO DA SIMULACAO ====================" << std::endl;
    std::cout << "Modo SDN: " << (enablePbr ? "SDN-PBR (Limiar 100ms)" : "Roteamento Padrao")
              << " | UEs: " << nUes << " | Handovers X2: " << g_handoverCount << std::endl;
    std::cout << "Downlink Video (4000): Tx=" << txPkts << " Rx=" << rxPkts
              << " Perda=" << std::fixed << std::setprecision(2) << dlLossPct << "%"
              << " Vazao=" << std::setprecision(4) << dlMbps << " Mbps"
              << " Atraso=" << std::setprecision(2) << dlDelayMs << " ms"
              << " Jitter=" << dlJitterMs << " ms" << std::endl;
    std::cout << "METRICAS_PBR," << (enablePbr ? "SDN-PBR" : "Roteamento_Padrao") << "," << nUes
              << "," << g_handoverCount << "," << dlMbps << "," << dlDelayMs << "," << dlJitterMs
              << "," << dlLossPct << std::endl;

    Simulator::Destroy();
    return 0;
}

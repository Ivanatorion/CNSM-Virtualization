#include "../include/pcheaders.h"
#include "../proto/p4runtime.grpc.pb.h"
#include "../include/PropertyList.h"
#include "../include/P4InfoDesc.h"
#include "../include/RuntimeCFG.h"
#include "../include/P4RuntimeImpl.h"
#include "../include/IPLBase.hpp"
#include "../include/Utils.h"

#define PI_PROGID_METADATAID 1

#define SLEEP(X) std::this_thread::sleep_for(std::chrono::milliseconds(X));

typedef struct _certs{
    std::string key;
    std::string cert;
    std::string root;
} Certificates;

Certificates getCertificates(){
    const std::vector<std::string> Paths = {"certificates/server.key", "certificates/server.crt", "certificates/VirtP4.crt"};
    std::string fileReads[3];

    for(int i = 0; i < 3; i++){
        FILE *fp = fopen(Paths[i].c_str(), "r");
        if(!fp){
            fprintf(stderr, "Error: Could not open file \"%s\"\n", Paths[i].c_str());
            exit(1);
        }
        while(!feof(fp)){
            char c = fgetc(fp);
            if(!feof(fp))
                fileReads[i].push_back(c);
        }
        fclose(fp);
    }

    return Certificates({fileReads[0], fileReads[1], fileReads[2]});
}

void parseArgs(int argc, char *argv[], bool *help, std::string* clientFilePath, std::string* virtMapFilePath, std::string* virtP4InfoPath, std::string* virtTargetAdd){
    int i = 1;
    while(i < argc){
        if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")){
            *help = true;
        }
        else if(!strcmp(argv[i], "--clientdata")){
            i++;
            if(i < argc)
                *clientFilePath = std::string(argv[i]);
        }
        else if(!strcmp(argv[i], "--virtmap")){
            i++;
            if(i < argc)
                *virtMapFilePath = std::string(argv[i]);
        }
        else if(!strcmp(argv[i], "--virtp4info")){
            i++;
            if(i < argc)
                *virtP4InfoPath = std::string(argv[i]);
        }
        else if(!strcmp(argv[i], "--targetaddr")){
            i++;
            if(i < argc)
                *virtTargetAdd = std::string(argv[i]);
        }
        else{
            fprintf(stderr, "Warning: Unrecognized option \"%s\"\n", argv[i]);
        }
        i++;
    }
}

void printHelp(char progName[]){
    printf("Usage: %s [OPTS] --virtp4info Virt.p4info.txt --virtmap VirtMap.cfg --clientdata ClientFile.cfg --targetaddr ip:port\n", progName);
    printf("\nOPTS:\n");
    printf("(--help | -h)          : Displays help message\n");
    printf("(--targetaddr ip:port) : Server addr of the P4runtime server of the target\n");
}

/*
void RunServerThread(const std::string serverAddr, Certificates certificates, P4RuntimeImpl* service){ 
    grpc::ServerBuilder builder;

	grpc::SslServerCredentialsOptions::PemKeyCertPair keycert =
	{
		certificates.key,
		certificates.cert
	};

	grpc::SslServerCredentialsOptions sslOps;
	sslOps.pem_root_certs = certificates.root;
	sslOps.pem_key_cert_pairs.push_back ( keycert );
    sslOps.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    sslOps.force_client_auth = true;

	builder.AddListeningPort(serverAddr, grpc::SslServerCredentials(sslOps));
	builder.RegisterService(service);

	std::unique_ptr < grpc::Server > server ( builder.BuildAndStart() );
	std::cout << "Server listening on " << serverAddr << std::endl;

	server->Wait();
}
*/

std::unique_ptr<grpc::Server> CreateServer(const std::string serverAddr, Certificates certificates, P4RuntimeImpl* service){ 
    grpc::ServerBuilder builder;

	grpc::SslServerCredentialsOptions::PemKeyCertPair keycert =
	{
		certificates.key,
		certificates.cert
	};

	grpc::SslServerCredentialsOptions sslOps;
	sslOps.pem_root_certs = certificates.root;
	sslOps.pem_key_cert_pairs.push_back ( keycert );
    sslOps.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    sslOps.force_client_auth = true;

	builder.AddListeningPort(serverAddr, grpc::SslServerCredentials(sslOps));
	builder.RegisterService(service);

	std::unique_ptr <grpc::Server> server ( builder.BuildAndStart() );
	std::cout << "Server listening on " << serverAddr << std::endl;

	return server;
}

void ProcessStreamChannelWithRunningSwitch(P4RuntimeImpl* P4RTServer, RuntimeCFG *runtimeCFG, std::shared_ptr<grpc::ClientReaderWriter<p4::v1::StreamMessageRequest, p4::v1::StreamMessageResponse>> stream, const int verboseLevel){
    p4::v1::StreamMessageResponse streamMessageResponse;
    
    while(stream->Read(&streamMessageResponse)){
        if(streamMessageResponse.has_packet()){
            if(verboseLevel >= VERBOSE_SIMPLE)
                printf("Received Packet-In\n");
            int program = 0;
            for(auto m: streamMessageResponse.packet().metadata()){
                if(m.metadata_id() == PI_PROGID_METADATAID){
                    Assert(m.value().size() == 1);
                    program = (int) m.value()[0];
                }
            }
            if(program > 0){
                streamMessageResponse.mutable_packet()->mutable_metadata()->Clear();
                P4RTServer->SendStreamResponseToClient(program, &streamMessageResponse);
            }
        }
        else if(streamMessageResponse.has_idle_timeout_notification()){
            if(verboseLevel >= VERBOSE_SIMPLE)
                printf("Received %d idle_timeout_notification(s)\n", streamMessageResponse.idle_timeout_notification().table_entry().size());
            for(int i = 0; i < (int) streamMessageResponse.mutable_idle_timeout_notification()->mutable_table_entry()->size(); i++){
                p4::v1::StreamMessageResponse partialResponse = streamMessageResponse;

                p4::v1::TableEntry* entry = partialResponse.mutable_idle_timeout_notification()->mutable_table_entry(i);
                
                int j = 0;
                while(j < partialResponse.mutable_idle_timeout_notification()->mutable_table_entry()->size()){
                    if(partialResponse.mutable_idle_timeout_notification()->mutable_table_entry(j) != entry){
                        partialResponse.mutable_idle_timeout_notification()->mutable_table_entry()->erase(partialResponse.mutable_idle_timeout_notification()->mutable_table_entry()->begin() + j);
                        j--;
                    }
                    j++;
                }
                
                VirtTableData vtd = runtimeCFG->GetVirtTableDataForTable(entry->table_id());
                int program = -1;
                if(verboseLevel >= VERBOSE_ALL)
                    Utils::PrintTableEntry(*entry);
                *entry = runtimeCFG->TranslateTableEntryVirtToClient(entry, &program);
                if(verboseLevel >= VERBOSE_ALL)
                    Utils::PrintTableEntry(*entry);
                P4RTServer->SendStreamResponseToClient(program, &partialResponse);
            }
        }
        else{
            printf("Other Response!\n");
        }
    }

    printf("Stream channel with target closed\n");
}

std::string BuildTofinoP4DeviceConfigString(const std::string& programName, const std::string& tofinoBinPath, const std::string& contextJSONPath){
    FILE *tofinoBin = fopen(tofinoBinPath.c_str(), "rb");
    if(!tofinoBin){
        fprintf(stderr, "Cannot open file: %s\n", tofinoBinPath.c_str());
        exit(1);
    }
    FILE *contextJSON = fopen(contextJSONPath.c_str(), "rb");
    if(!contextJSON){
        fclose(tofinoBin);
        fprintf(stderr, "Cannot open file: %s\n", contextJSONPath.c_str());
        exit(1);
    }

    std::string tofinoBinSTR;
    while(!feof(tofinoBin)){
        char c = fgetc(tofinoBin);
        if(!feof(tofinoBin))
            tofinoBinSTR.push_back(c);
    }
    std::string contextJSONSTR;
    while(!feof(contextJSON)){
        char c = fgetc(contextJSON);
        if(!feof(contextJSON))
            contextJSONSTR.push_back(c);
    }

    fclose(contextJSON);
    fclose(tofinoBin);

    const size_t ByteCount = 4 + programName.size() + 4 + tofinoBinSTR.size() + 4 + contextJSONSTR.size();

    char *buffer = new char[ByteCount];
    size_t ptr = 0;
    Utils::GetIntAs4Bytes(buffer, programName.size());
    ptr = ptr + 4;
    for(size_t x = 0; x < programName.size(); x++)
        buffer[ptr + x] = programName[x];
    ptr = ptr + programName.size();
    Utils::GetIntAs4Bytes(buffer + ptr, tofinoBinSTR.size());
    ptr = ptr + 4;
    for(size_t x = 0; x < tofinoBinSTR.size(); x++)
        buffer[ptr + x] = tofinoBinSTR[x];
    ptr = ptr + tofinoBinSTR.size();
    Utils::GetIntAs4Bytes(buffer + ptr, contextJSONSTR.size());
    ptr = ptr + 4;
    for(size_t x = 0; x < contextJSONSTR.size(); x++)
        buffer[ptr + x] = contextJSONSTR[x];
    std::string result(buffer, ByteCount);
    delete[] buffer;

    return result;
}

int main(int argc, char *argv[]){
    const std::string serverAddr = "localhost:9559";

    //std::string switchServerAddr = "143.54.51.26:9559";
    std::string switchServerAddr = "";
    
    std::string clientFilePath = "";
    std::string virtMapFilePath = "";
    std::string virtP4InfoFilePath = "";
    bool help = false;
    int verboseLevel = VERBOSE_ALL;

    parseArgs(argc, argv, &help, &clientFilePath, &virtMapFilePath, &virtP4InfoFilePath, &switchServerAddr);

    //if(help || clientFilePath == "" || virtMapFilePath == "" || virtP4InfoFilePath == "" || switchServerAddr == ""){
    if(help || switchServerAddr == "") {
        printHelp(argv[0]);
        return 0;
    }

    Certificates certificates = getCertificates();
    RuntimeCFG runtimeCFG(clientFilePath, virtMapFilePath, virtP4InfoFilePath);

    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(switchServerAddr, grpc::InsecureChannelCredentials());
    printf("Waiting for connection...\n");
    
    channel->WaitForConnected(gpr_time_from_seconds(10, gpr_clock_type::GPR_TIMESPAN));
    if(channel->GetState(false) != GRPC_CHANNEL_READY){
        fprintf(stderr, "Could not stabilish connection to %s\n", switchServerAddr.c_str());
        return 1;
    }
    
    std::shared_ptr<p4::v1::P4Runtime::Stub> stub = p4::v1::P4Runtime::NewStub(channel);
    grpc::ClientContext cContext;
    std::shared_ptr<grpc::ClientReaderWriter<p4::v1::StreamMessageRequest, p4::v1::StreamMessageResponse>> stream(stub->StreamChannel(&cContext));

    //Tofino
    /*    
    printf("Setting up Tofino Switch...\n");
    printf("Setting Forwarding Pipeline...\n");
    p4::v1::SetForwardingPipelineConfigRequest setPipeRequest;
    setPipeRequest.set_action(p4::v1::SetForwardingPipelineConfigRequest_Action::SetForwardingPipelineConfigRequest_Action_VERIFY_AND_COMMIT);
    setPipeRequest.set_device_id(0);
    *(setPipeRequest.mutable_config()->mutable_p4info()) = runtimeCFG.GetMergedP4Info();
    *(setPipeRequest.mutable_config()->mutable_p4_device_config()) = BuildTofinoP4DeviceConfigString("VirtMerged", "NOMSExp/Exp1/tofino.bin", "NOMSExp/Exp1/context.json");
    grpc::ClientContext cContext2;
    p4::v1::SetForwardingPipelineConfigResponse setPipeResponse;
    grpc::Status status = stub->SetForwardingPipelineConfig(&cContext2, setPipeRequest, &setPipeResponse);
    printf("Status Code: %d\nMSG: %s\n", (int) status.error_code(), status.error_message().c_str());
    if(status.error_code() != 0){
        return 1;
    }
    */

    p4::v1::StreamMessageRequest smr;
    p4::v1::StreamMessageResponse smresponse;
    smr.mutable_arbitration()->mutable_election_id()->set_low(0x1);
    smr.mutable_arbitration()->set_device_id(0);
    stream->Write(smr);
    stream->Read(&smresponse);
    printf("Status Code: %d\nMSG: %s\n", (int) smresponse.arbitration().status().code(), smresponse.arbitration().status().message().c_str());

    P4RuntimeImpl service(&runtimeCFG, stub, stream, verboseLevel);

    //std::thread serverThread(RunServerThread, serverAddr, certificates, &service);
    std::unique_ptr<grpc::Server> server = CreateServer(serverAddr, certificates, &service);
    std::thread streamResponses(ProcessStreamChannelWithRunningSwitch, &service, &runtimeCFG, stream, verboseLevel);

    streamResponses.join();
    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));

    return 0;
}

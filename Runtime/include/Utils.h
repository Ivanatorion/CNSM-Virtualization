#ifndef VIRT_P4RT_UTILS_H
#define VIRT_P4RT_UTILS_H

#include "../include/PropertyList.h"

namespace Utils {

extern bool EnablePrints;
extern std::string TabSpaces;
extern int TabIncrease;
extern FILE* OutputStream;

void SetOutputStream(FILE *os);

//Returns a string representing the bytes of the given string
//Such as {8,0,0,0,1,2} -> "\010\000\000\000\001\002"
std::string StringBytes(const std::string& str);

std::string UpdateTypeToString(const p4::v1::Update_Type update);
std::string WriteRequestAtomicityToString(const p4::v1::WriteRequest_Atomicity atomicity);

void PrintFieldMatch(const p4::v1::FieldMatch& match);
void PrintAction(const p4::v1::Action& action);
void PrintTableAction(const p4::v1::TableAction& table_action);
void PrintExternEntry(const p4::v1::ExternEntry& extern_entry);
void PrintTableEntry(const p4::v1::TableEntry& table_entry);
void PrintActionProfileMember(const p4::v1::ActionProfileMember& action_profile_member);
void PrintActionProfileGroup(const p4::v1::ActionProfileGroup& action_profile_group);
void PrintMeterEntry(const p4::v1::MeterEntry& meter_entry);
void PrintDirectMeterEntry(const p4::v1::DirectMeterEntry& direct_meter_entry);
void PrintCounterEntry(const p4::v1::CounterEntry& counter_entry);
void PrintDirectCounterEntry(const p4::v1::DirectCounterEntry& direct_counter_entry);
void PrintPacketReplicationEngineEntry(const p4::v1::PacketReplicationEngineEntry& packet_replication_engine_entry);
void PrintValueSetEntry(const p4::v1::ValueSetEntry& value_set_entry);
void PrintRegisterEntry(const p4::v1::RegisterEntry& register_entry);
void PrintDigestEntry(const p4::v1::DigestEntry& digest_entry);
void PrintEntity(const p4::v1::Entity& entity);
void PrintUpdate(const p4::v1::Update& update);
void PrintWriteRequest(const p4::v1::WriteRequest* request);
void PrintReadRequest(const p4::v1::ReadRequest* request);
void PrintReadResponse(const p4::v1::ReadResponse* response);

//P4Info
void PrintPreamble(const p4::config::v1::Preamble& preamble);
void PrintMatchField(const p4::config::v1::MatchField& match_field);
void PrintActionRef(const p4::config::v1::ActionRef& action_ref);
void PrintActionParam(const p4::config::v1::Action_Param& param);
void PrintAction(const p4::config::v1::Action& action);
void PrintTable(const p4::config::v1::Table& table);
void PrintPkgInfo(const p4::config::v1::PkgInfo& pkginfo);
void PrintControllerPacketMetadataMetadata(const p4::config::v1::ControllerPacketMetadata_Metadata& metadata);
void PrintControllerPacketMetadata(const p4::config::v1::ControllerPacketMetadata& cpm);
void PrintP4Info(const p4::config::v1::P4Info& p4info);

//Input is an arbitrary sequence of 0s and 1s
std::string binaryToBytes(const std::string& binaryString);

//Input should have size 8 with 0s and 1s
char binaryStringToByte(const std::string& byteString);

int byteStringToInt(const std::string& byteString);

inline void SetOutputStream(FILE* os){
    Utils::OutputStream = os;
}

//Populates the 4 byte buffer with the binary representation of "i" in little endian
inline void GetIntAs4Bytes(char *buffer4Bytes, const int i) {
    int mask = 255;
    for(int x = 0; x < 4; x++){
        buffer4Bytes[x] = (char) ((i & mask) >> (x * 8));
        mask = mask << 8;
    }
}

//Returns the integer in little endian representation in the 4 byte buffer
inline int Get4BytesAsInt(const char *buffer4Bytes) {return *((int*) buffer4Bytes);}

} //namespace Utils

#endif
#include "../include/pcheaders.h"
#include "../include/AST.h"
#include "../include/TofinoMerge.h"
#include "../include/Log.h"
#include "../include/CFG.h"

#include "../include/IPLBase.hpp"

#define PROGRAM_ID_TYPE_BITS_STR "8"
#define METADATA_NAME "VirtMetadata"
#define INGRESS_METADATA_TYPE_NAME "VirtIngressMetadataType"
#define EGRESS_METADATA_TYPE_NAME "VirtEgressMetadataType"
#define METADATA_PROG_ID_FIELD_NAME "VirtProgramID"
#define SINGLE_PIPELINE_NAME "VirtPipe"

#define PI_PARAM_TYPE "packet_in"
#define PI_PARAM_NAME "pkt"
#define PO_PARAM_TYPE "packet_out"
#define PO_PARAM_NAME "pkt"
#define IG_INTRINSIC_MD_PARAM_TYPE "ingress_intrinsic_metadata_t"
#define IG_INTRINSIC_MD_PARAM_NAME "ig_intr_md"
#define IG_INTRINSIC_MD_PARSER_PARAM_TYPE "ingress_intrinsic_metadata_from_parser_t"
#define IG_INTRINSIC_MD_PARSER_PARAM_NAME "ig_intr_prsr_md"
#define IG_INTRINSIC_MD_DEPARSER_PARAM_TYPE "ingress_intrinsic_metadata_for_deparser_t"
#define IG_INTRINSIC_MD_DEPARSER_PARAM_NAME "ig_intr_dprsr_md"
#define IG_INTRINSIC_MD_TM_PARAM_TYPE "ingress_intrinsic_metadata_for_tm_t"
#define IG_INTRINSIC_MD_TM_PARAM_NAME "ig_intr_tm_md"
#define EG_INTRINSIC_MD_PARAM_TYPE "egress_intrinsic_metadata_t"
#define EG_INTRINSIC_MD_PARAM_NAME "eg_intr_md"
#define EG_INTRINSIC_MD_PARSER_PARAM_TYPE "egress_intrinsic_metadata_from_parser_t"
#define EG_INTRINSIC_MD_PARSER_PARAM_NAME "eg_intr_from_prsr"
#define EG_INTRINSIC_MD_DEPARSER_PARAM_TYPE "egress_intrinsic_metadata_for_deparser_t"
#define EG_INTRINSIC_MD_DEPARSER_PARAM_NAME "eg_intr_md_for_dprsr"
#define EG_INTRINSIC_MD_OP_PARAM_TYPE "egress_intrinsic_metadata_for_output_port_t"
#define EG_INTRINSIC_MD_OP_PARAM_NAME "eg_intr_md_for_oport"

void RenameVirtMetadataVariables(AST *ast, const std::string& metadataTypeName, const std::string& newName){
    if(ast->getProgramType() == metadataTypeName)
        ast->setValue(newName);
    for(AST *c : ast->getChildren())
        if(c != NULL)
            RenameVirtMetadataVariables(c, metadataTypeName, newName);
}

void RemoveTempProgramCode(AST *ast, const std::string& metadataTypeName, const std::string& newName){
    if(ast->getNodeType() == NT_EXPRESSION && ast->getValue() == TEMP_PROGRAM_CODE){
        ast->setValue(".");
        ast->addChild(new AST(newName, NT_EXPRESSION));
        ast->addChild(new AST(METADATA_PROG_ID_FIELD_NAME, NT_NONE));
        ast->getChildren()[1]->setProgramType(metadataTypeName);
    }
    for(AST *c : ast->getChildren())
        if(c != NULL)
            RemoveTempProgramCode(c, metadataTypeName, newName);
}

bool TofinoMerge::MergeTofino(const std::vector<AST*> programs, const std::vector<AST*>& headersDec, const std::vector<std::set<int>>& virtualSwitchPorts, const int cpuPort){
    CFG::SetMetadataProgIDName(std::string(METADATA_NAME "." METADATA_PROG_ID_FIELD_NAME));
    
    AST* LastProg = programs[programs.size() - 1];

    std::string msg = "Merging Tofino Programs:";
    for(AST *program : programs)
        msg = msg + " \"" + program->value + "\"";
    Log::MSG(msg);
    Log::Push();

    std::vector<std::vector<AST*>> Pipelines(programs.size());

    int nPipes = -1;
    for(int i = 0; i < (int) programs.size(); i++){
        if(!GetPipelines(programs[i], &Pipelines[i])){
            Log::Pop();
            return false;
        }
        if(nPipes == -1)
            nPipes = (int) Pipelines[i].size();
        else if(nPipes != (int) Pipelines[i].size()){
            fprintf(stderr, "TofinoMerge: Different number of Switch Pipelines\n");
            Log::Pop();
            return false;
        }
        
        TofinoMerge::RemoveSwitchInstantiation(programs[i]);
    }
    
    for(int i = 0; i < (int) Pipelines[0].size(); i++){
        std::vector<AST*> pipelinesToMerge;
        for(int j = 0; j < (int) programs.size(); j++)
            pipelinesToMerge.push_back(Pipelines[j][i]);
        bool result = TofinoMerge::MergePipelines(programs, pipelinesToMerge, headersDec, virtualSwitchPorts, cpuPort);
        if(!result){
            Log::Pop();
            return false;
        }
    }

    //Construct new Switch instantiation
    Assert(Pipelines[0].size() == 1);
    AST* newSwitch = new AST("main", NT_INSTANTIATION);
    newSwitch->addChild(NULL); //annotations
    newSwitch->addChild(new AST("Switch", NT_TYPE_NAME));
    newSwitch->addChild(new AST(NULL, NT_ARGUMENT_LIST));
    newSwitch->getChildNT(NT_ARGUMENT_LIST)->addChild(new AST(NULL, NT_ARGUMENT));
    newSwitch->getChildNT(NT_ARGUMENT_LIST)->children[0]->addChild(new AST(SINGLE_PIPELINE_NAME, NT_EXPRESSION));
    newSwitch->getChildNT(NT_ARGUMENT_LIST)->children[0]->addChild(NULL);
    LastProg->addChild(newSwitch);

    //Change Switch Local Metadata parameters to METADATA_NAME
    /*
    const std::unordered_map<std::string, int> VirtPipelineStructNames = {{"VirtIngressParser", 2}, {"VirtEgressParser", 2}, {"VirtIngress", 1}, {"VirtEgress", 1}, {"VirtIngressDeparser", 2}, {"VirtEgressDeparser", 2}};
    for(int i = 0; i < (int) LastProg->children.size(); i++){
        if(LastProg->children[i]->nType == NT_PARSER_DECLARATION && VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->value) != VirtPipelineStructNames.end()){
            const std::string vMetadataHeaderName = LastProg->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->value)->second]->getChildNT(NT_TYPE_NAME)->value;
            LastProg->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->value)->second]->value = METADATA_NAME;
            
            AST *newField = new AST(METADATA_PROG_ID_FIELD_NAME, NT_STRUCT_FIELD);
            newField->addChild(NULL); //optAnnotations
            newField->addChild(new AST("bit", NT_BASE_TYPE));
            newField->children[1]->addChild(new AST("8", NT_LITERAL));
            
            int pp = 0;
            while(pp < (int) LastProg->children.size() && ((LastProg->children[pp]->nType != NT_HEADER_TYPE_DECLARATION && LastProg->children[pp]->nType != NT_STRUCT_TYPE_DECLARATION) || LastProg->children[pp]->value != vMetadataHeaderName))
                pp++;
            if(pp == (int) LastProg->children.size()){
                pp = 0;
                while(pp < (int) headersDec.size() && ((headersDec[pp]->nType != NT_HEADER_TYPE_DECLARATION && headersDec[pp]->nType != NT_STRUCT_TYPE_DECLARATION) || headersDec[pp]->value != vMetadataHeaderName))
                    pp++;
                Assert(pp < (int) headersDec.size());
                headersDec[pp]->writeCode(stdout);
                if(headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST) != NULL){
                    bool alreadyHas = false;
                    for(int sf = 0; sf < (int) headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children.size(); sf++){
                        if(headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children[sf]->value == METADATA_PROG_ID_FIELD_NAME)
                            alreadyHas = true;
                    }
                    if(!alreadyHas){
                        headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->addChild(newField->Clone());
                    }
                }
                else{
                    AST *newSFL = new AST(NULL, NT_STRUCT_FIELD_LIST);
                    newSFL->addChild(newField->Clone());
                    headersDec[pp]->children[headersDec[pp]->children.size() - 1] = newSFL;
                }
                LastProg->SubstituteTypeName(headersDec[pp]->value, METADATA_TYPE_NAME);
                headersDec[pp]->value = METADATA_TYPE_NAME;
            }
            else{
                LastProg->children[pp]->writeCode(stdout);
                if(LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST) == NULL)
                    LastProg->children[pp]->children[2] = new AST("", NT_STRUCT_FIELD_LIST);
                
                bool alreadyHas = false;
                for(int sf = 0; sf < (int) LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children.size(); sf++){
                    if(LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children[sf]->value == METADATA_PROG_ID_FIELD_NAME)
                        alreadyHas = true;
                }
                if(!alreadyHas){
                    LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->addChild(newField->Clone());
                }

                LastProg->SubstituteTypeName(LastProg->children[pp]->value, METADATA_TYPE_NAME);
                LastProg->children[pp]->value = METADATA_TYPE_NAME;
            }

            delete newField;
        }
        else if(LastProg->children[i]->nType == NT_CONTROL_DECLARATION && VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(NT_CONTROL_TYPE_DECLARATION)->value) != VirtPipelineStructNames.end()){
            const std::string vMetadataHeaderName = LastProg->children[i]->getChildNT(NT_CONTROL_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(NT_CONTROL_TYPE_DECLARATION)->value)->second]->getChildNT(NT_TYPE_NAME)->value;
            LastProg->children[i]->getChildNT(NT_CONTROL_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(NT_CONTROL_TYPE_DECLARATION)->value)->second]->value = METADATA_NAME;
            
            AST *newField = new AST(METADATA_PROG_ID_FIELD_NAME, NT_STRUCT_FIELD);
            newField->addChild(NULL); //optAnnotations
            newField->addChild(new AST("bit", NT_BASE_TYPE));
            newField->children[1]->addChild(new AST("8", NT_LITERAL));
            
            int pp = 0;
            while(pp < (int) LastProg->children.size() && ((LastProg->children[pp]->nType != NT_HEADER_TYPE_DECLARATION && LastProg->children[pp]->nType != NT_STRUCT_TYPE_DECLARATION) || LastProg->children[pp]->value != vMetadataHeaderName))
                pp++;
            if(pp == (int) LastProg->children.size()){
                pp = 0;
                while(pp < (int) headersDec.size() && ((headersDec[pp]->nType != NT_HEADER_TYPE_DECLARATION && headersDec[pp]->nType != NT_STRUCT_TYPE_DECLARATION) || headersDec[pp]->value != vMetadataHeaderName))
                    pp++;
                Assert(pp < (int) headersDec.size());
                if(headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST) != NULL){
                    bool alreadyHas = false;
                    for(int sf = 0; sf < (int) headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children.size(); sf++){
                        if(headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children[sf]->value == METADATA_PROG_ID_FIELD_NAME)
                            alreadyHas = true;
                    }
                    if(!alreadyHas){
                        headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->addChild(newField->Clone());
                    }
                }
                else{
                    AST *newSFL = new AST(NULL, NT_STRUCT_FIELD_LIST);
                    newSFL->addChild(newField->Clone());
                    headersDec[pp]->children[headersDec[pp]->children.size() - 1] = newSFL;
                }
                LastProg->SubstituteTypeName(headersDec[pp]->value, METADATA_TYPE_NAME);   
                headersDec[pp]->value = METADATA_TYPE_NAME;
            }
            else{
                if(LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST) == NULL)
                    LastProg->children[pp]->children[2] = new AST("", NT_STRUCT_FIELD_LIST);
                
                bool alreadyHas = false;
                for(int sf = 0; sf < (int) LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children.size(); sf++){
                    if(LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children[sf]->value == METADATA_PROG_ID_FIELD_NAME)
                        alreadyHas = true;
                }
                if(!alreadyHas){
                    LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->addChild(newField->Clone());
                }

                LastProg->SubstituteTypeName(LastProg->children[pp]->value, METADATA_TYPE_NAME);
                LastProg->children[pp]->value = METADATA_TYPE_NAME;
            }

            delete newField;
        }
    }
    RenameVirtMetadataVariables(LastProg);

    //Change generic TEMP_PROGRAM_CODE to METADATA_NAME.METADATA_PROG_ID_FIELD_NAME
    RemoveTempProgramCode(LastProg);
    */
    const std::unordered_map<std::string, int> VirtPipelineStructNames = {{"VirtIngressParser", 2}, {"VirtEgressParser", 2}, {"VirtIngress", 1}, {"VirtEgress", 1}, {"VirtIngressDeparser", 2}, {"VirtEgressDeparser", 2}};
    const std::unordered_map<std::string, int> VirtIngressPipelineStructNames = {{"VirtIngressParser", 2}, {"VirtIngress", 1}, {"VirtIngressDeparser", 2}};
    const std::unordered_map<std::string, int> VirtEgressPipelineStructNames = {{"VirtEgressParser", 2}, {"VirtEgress", 1}, {"VirtEgressDeparser", 2}};
    for(int i = 0; i < (int) LastProg->children.size(); i++){
        bool isIngresStruct = VirtIngressPipelineStructNames.find(LastProg->children[i]->value) != VirtIngressPipelineStructNames.end();
        bool isEgresStruct = VirtEgressPipelineStructNames.find(LastProg->children[i]->value) != VirtIngressPipelineStructNames.end();
        if(isIngresStruct || isEgresStruct){
            const NodeType nTypeDec = (LastProg->children[i]->nType == NT_PARSER_DECLARATION) ? NT_PARSER_TYPE_DECLARATION : NT_CONTROL_TYPE_DECLARATION;
            const std::string structMetadataTypeName = isIngresStruct ? INGRESS_METADATA_TYPE_NAME : EGRESS_METADATA_TYPE_NAME;
            const std::string vMetadataHeaderName = LastProg->children[i]->getChildNT(nTypeDec)->getChildNT(NT_PARAMETER_LIST)->children[VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(nTypeDec)->value)->second]->getChildNT(NT_TYPE_NAME)->value;
            LastProg->children[i]->getChildNT(nTypeDec)->getChildNT(NT_PARAMETER_LIST)->children[VirtPipelineStructNames.find(LastProg->children[i]->getChildNT(nTypeDec)->value)->second]->value = METADATA_NAME;
            
            AST *newField = new AST(METADATA_PROG_ID_FIELD_NAME, NT_STRUCT_FIELD);
            newField->addChild(NULL); //optAnnotations
            newField->addChild(new AST("bit", NT_BASE_TYPE));
            newField->children[1]->addChild(new AST("8", NT_LITERAL));
            
            int pp = 0;
            while(pp < (int) LastProg->children.size() && ((LastProg->children[pp]->nType != NT_HEADER_TYPE_DECLARATION && LastProg->children[pp]->nType != NT_STRUCT_TYPE_DECLARATION) || LastProg->children[pp]->value != vMetadataHeaderName))
                pp++;
            if(pp == (int) LastProg->children.size()){
                pp = 0;
                while(pp < (int) headersDec.size() && ((headersDec[pp]->nType != NT_HEADER_TYPE_DECLARATION && headersDec[pp]->nType != NT_STRUCT_TYPE_DECLARATION) || headersDec[pp]->value != vMetadataHeaderName))
                    pp++;
                Assert(pp < (int) headersDec.size());
                if(headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST) != NULL){
                    bool alreadyHas = false;
                    for(int sf = 0; sf < (int) headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children.size(); sf++){
                        if(headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children[sf]->value == METADATA_PROG_ID_FIELD_NAME)
                            alreadyHas = true;
                    }
                    if(!alreadyHas){
                        headersDec[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->addChild(newField->Clone());
                    }
                }
                else{
                    AST *newSFL = new AST(NULL, NT_STRUCT_FIELD_LIST);
                    newSFL->addChild(newField->Clone());
                    headersDec[pp]->children[headersDec[pp]->children.size() - 1] = newSFL;
                }
                LastProg->SubstituteTypeName(headersDec[pp]->value, structMetadataTypeName);
                headersDec[pp]->value = structMetadataTypeName;
            }
            else{
                if(LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST) == NULL)
                    LastProg->children[pp]->children[2] = new AST("", NT_STRUCT_FIELD_LIST);
                
                bool alreadyHas = false;
                for(int sf = 0; sf < (int) LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children.size(); sf++){
                    if(LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->children[sf]->value == METADATA_PROG_ID_FIELD_NAME)
                        alreadyHas = true;
                }
                if(!alreadyHas){
                    LastProg->children[pp]->getChildNT(NT_STRUCT_FIELD_LIST)->addChild(newField->Clone());
                }

                LastProg->SubstituteTypeName(LastProg->children[pp]->value, structMetadataTypeName);
                LastProg->children[pp]->value = structMetadataTypeName;
            }

            delete newField;
            RenameVirtMetadataVariables(LastProg->children[i], structMetadataTypeName, METADATA_NAME);
            RemoveTempProgramCode(LastProg->children[i], structMetadataTypeName, METADATA_NAME);
        }
    }

    

    Log::Pop();
    return true;
}

bool TofinoMerge::RemoveIngressPortMetadataExtractions(const std::string& programName, AST *statelist, AST *state, const std::string packetInName, const std::string parameterName, bool intrinsicMDportMD){
    AST *statementList = state->getChildNT(NT_PARSER_STATEMENT_LIST);

    if(statementList != NULL){
        int i = 0;
        while(i < (int) statementList->children.size()){
            AST *statement = statementList->children[i];
            if(statement->nType == NT_METHOD_CALL_STATEMENT){
                if(statement->children[0]->children[1]->value == "extract"){
                    if(intrinsicMDportMD){
                        AST *argList = statement->getChildNT(NT_ARGUMENT_LIST);
                        if(argList != NULL && argList->children.size() == 1 && argList->children[0]->children[0]->value == parameterName){
                            delete statement;
                            statementList->children.erase(statementList->children.begin() + i);
                            i--;

                            intrinsicMDportMD = false;
                        }
                        else{
                            fprintf(stderr, "Error: Ingress parser first packet_in extract must be of \"%s\" in program \"%s\"\n", parameterName.c_str(), programName.c_str());
                            exit(1);
                        }
                    }
                    else{
                        fprintf(stderr, "Error: Ingress parser must skip (advance) the TNA Port Metadata in program \"%s\"\n", programName.c_str());
                        exit(1);
                    }
                }
                else if(statement->children[0]->children[1]->value == "advance"){
                    delete statement;
                    statementList->children.erase(statementList->children.begin() + i);
                    i--;

                    if(statementList->children.size() == 0){
                        delete statementList;
                        state->children[1] = NULL;
                    }

                    return true;
                }
            }

            i++;
        }
        if(statementList->children.size() == 0){
            delete statementList;
            state->children[1] = NULL;
        }
    }
    if(state->getChildNT(NT_NAME_STATE_EXPRESSION) != NULL){
        AST *nextState = statelist->FindStateInList(state->getChildNT(NT_NAME_STATE_EXPRESSION)->value);
        if(nextState == NULL){
            fprintf(stderr, "Error: State not found in ingress parser: %s (\"%s\")\n", state->getChildNT(NT_NAME_STATE_EXPRESSION)->value.c_str(), programName.c_str());
            exit(1);
        }
        return TofinoMerge::RemoveIngressPortMetadataExtractions(programName, statelist, nextState, packetInName, parameterName, intrinsicMDportMD);
    }
    else{
        AST *SCL = state->getChildNT(NT_SELECT_EXPRESSION)->getChildNT(NT_SELECT_CASE_LIST);
        for(AST *sc : SCL->children){
            if(sc->value != "accept" && sc->value != "reject"){
                AST *nextState = statelist->FindStateInList(sc->value);
                if(nextState == NULL){
                    fprintf(stderr, "Error: State not found in ingress parser: %s (\"%s\")\n", state->getChildNT(NT_NAME_STATE_EXPRESSION)->value.c_str(), programName.c_str());
                    exit(1);
                }
                bool result = TofinoMerge::RemoveIngressPortMetadataExtractions(programName, statelist, nextState, packetInName, parameterName, intrinsicMDportMD);
                if(!result){
                    fprintf(stderr, "Error: Program \"%s\" must extract the ingress intrinsic metadata in the ingress parser and skip (advance) the Port Metadata field\n", programName.c_str());
                    exit(1);
                }
            }
        }
        return true;
    }

    return false;
}

bool TofinoMerge::RemoveEgressPortMetadataExtractions(const std::string& programName, AST *statelist, AST *state, const std::string packetInName, const std::string parameterName){
    AST *statementList = state->getChildNT(NT_PARSER_STATEMENT_LIST);

    if(statementList != NULL){
        int i = 0;
        while(i < (int) statementList->children.size()){
            AST *statement = statementList->children[i];
            if(statement->nType == NT_METHOD_CALL_STATEMENT){
                if(statement->children[0]->children[1]->value == "extract"){
                    AST *argList = statement->getChildNT(NT_ARGUMENT_LIST);
                    if(argList != NULL && argList->children.size() == 1 && argList->children[0]->children[0]->value == parameterName){
                        delete statement;
                        statementList->children.erase(statementList->children.begin() + i);
                        i--;
                        return true;
                    }
                    else{
                        fprintf(stderr, "Error: Egress parser first packet_in extract must be of \"%s\" in program \"%s\"\n", parameterName.c_str(), programName.c_str());
                        exit(1);
                    }
                }
            }
            i++;
        }
        if(statementList->children.size() == 0){
            delete statementList;
            state->children[1] = NULL;
        }
    }
    if(state->getChildNT(NT_NAME_STATE_EXPRESSION) != NULL){
        AST *nextState = statelist->FindStateInList(state->getChildNT(NT_NAME_STATE_EXPRESSION)->value);
        if(nextState == NULL){
            fprintf(stderr, "Error: State not found in egress parser: %s (\"%s\")\n", state->getChildNT(NT_NAME_STATE_EXPRESSION)->value.c_str(), programName.c_str());
            exit(1);
        }
        return TofinoMerge::RemoveEgressPortMetadataExtractions(programName, statelist, nextState, packetInName, parameterName);
    }
    else{
        AST *SCL = state->getChildNT(NT_SELECT_EXPRESSION)->getChildNT(NT_SELECT_CASE_LIST);
        for(AST *sc : SCL->children){
            if(sc->value != "accept" && sc->value != "reject"){
                AST *nextState = statelist->FindStateInList(sc->value);
                if(nextState == NULL){
                    fprintf(stderr, "Error: State not found in egress parser: %s (\"%s\")\n", state->getChildNT(NT_NAME_STATE_EXPRESSION)->value.c_str(), programName.c_str());
                    exit(1);
                }
                bool result = TofinoMerge::RemoveEgressPortMetadataExtractions(programName, statelist, nextState, packetInName, parameterName);
                if(!result){
                    fprintf(stderr, "Error: Program \"%s\" must extract the egress intrinsic metadata in the egress parser\n", programName.c_str());
                    exit(1);
                }
            }
        }
        return true;
    }

    return false;
}

bool TofinoMerge::MergePipelines(const std::vector<AST*>& programs, const std::vector<AST*>& pipelines, const std::vector<AST*>& headersDec, const std::vector<std::set<int>>& virtualSwitchPorts, const int cpuPort){
    const static std::unordered_map<std::string, std::string> ParamNames = {
        {PI_PARAM_TYPE, PI_PARAM_NAME},
        {PO_PARAM_TYPE, PO_PARAM_NAME},
        {IG_INTRINSIC_MD_PARAM_TYPE, IG_INTRINSIC_MD_PARAM_NAME},
        {IG_INTRINSIC_MD_PARSER_PARAM_TYPE, IG_INTRINSIC_MD_PARSER_PARAM_NAME},
        {IG_INTRINSIC_MD_DEPARSER_PARAM_TYPE, IG_INTRINSIC_MD_DEPARSER_PARAM_NAME},
        {IG_INTRINSIC_MD_TM_PARAM_TYPE, IG_INTRINSIC_MD_TM_PARAM_NAME},
        {EG_INTRINSIC_MD_PARAM_TYPE, EG_INTRINSIC_MD_PARAM_NAME},
        {EG_INTRINSIC_MD_PARSER_PARAM_TYPE, EG_INTRINSIC_MD_PARSER_PARAM_NAME},
        {EG_INTRINSIC_MD_DEPARSER_PARAM_TYPE, EG_INTRINSIC_MD_DEPARSER_PARAM_NAME},
        {EG_INTRINSIC_MD_OP_PARAM_TYPE, EG_INTRINSIC_MD_OP_PARAM_NAME},
    };

    std::vector<AST*> nodesToAdd;
    
    //Parsers
    const std::vector<int> parserParams = {0, 3};
    const std::vector<std::string> parserNames = {"VirtIngressParser", "VirtEgressParser"};
    int pCounter = 0;
    for(int parserParam : parserParams){
        std::vector<AST*> elements;
        std::vector<std::string> elementNames;
        for(AST* pipeline : pipelines)
            elementNames.push_back(pipeline->getChildNT(NT_ARGUMENT_LIST)->children[parserParam]->children[0]->value);

        for(int p = 0; p < (int) programs.size(); p++){
            int i = 0;
            while(i < (int) programs[p]->children.size() && (programs[p]->children[i]->nType != NT_PARSER_DECLARATION || programs[p]->children[i]->children[0]->value != elementNames[p]))
                i++;
            if(i == (int) programs[p]->children.size()){
                fprintf(stderr, "TofinoMerge: Could not find \"%s\" Parser declaration\n", elementNames[p].c_str());
                return false;
            }

            if(cpuPort > 0){
                for(AST *a : headersDec){
                    if(a->value == programs[p]->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[1]->getChildNT(NT_TYPE_NAME)->value){
                        bool hasPacketInHeader = false;
                        
                        if(a->getChildNT(NT_STRUCT_FIELD_LIST) == NULL)
                            a->children[2] = new AST(NULL, NT_STRUCT_FIELD_LIST);

                        for(AST *SF : a->getChildNT(NT_STRUCT_FIELD_LIST)->children)
                            if(SF->getChildNT(NT_TYPE_NAME) != NULL && SF->getChildNT(NT_TYPE_NAME)->value == VIRT_PACKET_IN_HEADER_TYPE)
                                hasPacketInHeader = true;
                        
                        if(!hasPacketInHeader){
                            AST *SF = new AST(std::string(VIRT_PACKET_IN_HEADER_NAME), NT_STRUCT_FIELD);
                            SF->addChild(NULL); //optAnnotations
                            SF->addChild(new AST(VIRT_PACKET_IN_HEADER_TYPE, NT_TYPE_NAME));
                            a->getChildNT(NT_STRUCT_FIELD_LIST)->addChild(SF);
                        }
                    }
                }
            }

            //Add statement at the start to set program id
            for(AST *state : programs[p]->children[i]->getChildNT(NT_PARSER_STATE_LIST)->children){
                if(state->value == "start"){
                    //Removes the extraction / advance of intrinsic metadata
                    if(parserParam == 0)
                        TofinoMerge::RemoveIngressPortMetadataExtractions(programs[p]->value, programs[p]->children[i]->getChildNT(NT_PARSER_STATE_LIST), state, programs[p]->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[0]->value, programs[p]->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[3]->value, true);
                    else
                        TofinoMerge::RemoveEgressPortMetadataExtractions(programs[p]->value, programs[p]->children[i]->getChildNT(NT_PARSER_STATE_LIST), state, programs[p]->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[0]->value, programs[p]->children[i]->getChildNT(NT_PARSER_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[3]->value);

                    char buffer[128];
                    AST *setIDStatement = new AST("=", NT_ASSIGN_STATEMENT);
                    AST *leftSide = new AST(".", NT_NONE);
                    leftSide->addChild(new AST(METADATA_NAME, NT_NONE));
                    leftSide->addChild(new AST(METADATA_PROG_ID_FIELD_NAME, NT_NONE));
                    setIDStatement->addChild(leftSide);
                    sprintf(buffer, "%d", p + 1);
                    setIDStatement->addChild(new AST(std::string(buffer), NT_EXPRESSION));
                    
                    AST *sl = state->getChildNT(NT_PARSER_STATEMENT_LIST);
                    if(sl == NULL){
                        sl = new AST(NULL, NT_PARSER_STATEMENT_LIST);
                        state->children[1] = sl;
                    }
                    sl->addChildStart(setIDStatement);
                }
            }

            programs[p]->children[i]->RemoveSimpleParserStates();

            elements.push_back(programs[p]->children[i]);
        }

        AST *newExpression = new AST(".", NT_EXPRESSION);
        AST *newExpressionL = new AST(parserParam == 0 ? IG_INTRINSIC_MD_PARAM_NAME : EG_INTRINSIC_MD_PARAM_NAME, NT_EXPRESSION);
        AST *newExpressionR = new AST(parserParam == 0 ? "ingress_port" : "egress_port", NT_NONE);
        newExpression->addChild(newExpressionL);
        newExpression->addChild(newExpressionR);
        std::vector<AST*> newNodes = AST::MergeParsers(programs, elements, headersDec, parserNames[pCounter], virtualSwitchPorts, newExpression);
        
        delete newExpression;
        if(newNodes.size() == 0)
            return false;

        AST *parser = newNodes[newNodes.size() - 1];

        //Change the names of the parameters
        for(AST *param : parser->children[0]->children[2]->children){
            auto it = ParamNames.find(AST::RemoveMergePrefix(param->children[2]->value));
            if(it != ParamNames.end()){
                std::string val = param->value;
                if(parser->children[2] != NULL)
                    parser->children[2]->SubstituteIdentifierWithType(val, it->second, param->children[2]->value);
                if(parser->children[3] != NULL)
                    parser->children[3]->SubstituteIdentifierWithType(val, it->second, param->children[2]->value);
                param->value = it->second;
            }
        }

        AST *startState = NULL;
        for(AST *state : parser->getChildNT(NT_PARSER_STATE_LIST)->children)
            if(state->value == "start")
                startState = state;
        Assert(startState != NULL);
        Assert(startState->getChildNT(NT_PARSER_STATEMENT_LIST) == NULL);

        const std::string packetParamName = parser->children[0]->children[2]->children[0]->value;

        AST *transitionSelectExpression = new AST(".", NT_EXPRESSION);
        
        if(parserParam == 0){ //Ingress
            AST *extractExpression = new AST(NULL, NT_METHOD_CALL_STATEMENT);
            extractExpression->addChild(new AST(".", NT_EXPRESSION));
            extractExpression->children[0]->addChild(new AST(packetParamName, NT_EXPRESSION));
            extractExpression->children[0]->addChild(new AST("extract", NT_NONE));
            extractExpression->addChild(NULL);
            extractExpression->addChild(new AST(NULL, NT_ARGUMENT_LIST));
            extractExpression->children[2]->addChild(new AST(NULL, NT_ARGUMENT));
            extractExpression->children[2]->children[0]->addChild(new AST(IG_INTRINSIC_MD_PARAM_NAME, NT_EXPRESSION));

            AST *advanceExpression = new AST(NULL, NT_METHOD_CALL_STATEMENT);
            advanceExpression->addChild(new AST(".", NT_EXPRESSION));
            advanceExpression->children[0]->addChild(new AST(packetParamName, NT_EXPRESSION));
            advanceExpression->children[0]->addChild(new AST("advance", NT_NONE));
            advanceExpression->addChild(NULL);
            advanceExpression->addChild(new AST(NULL, NT_ARGUMENT_LIST));
            advanceExpression->children[2]->addChild(new AST(NULL, NT_ARGUMENT));
            advanceExpression->children[2]->children[0]->addChild(new AST("PORT_METADATA_SIZE", NT_EXPRESSION));

            if(startState->getChildNT(NT_PARSER_STATEMENT_LIST) == NULL){
                startState->children[1] = new AST(NULL, NT_PARSER_STATEMENT_LIST);
                startState->children[1]->addChild(extractExpression);
                startState->children[1]->addChild(advanceExpression);
            }

            transitionSelectExpression->addChild(new AST(IG_INTRINSIC_MD_PARAM_NAME, NT_NONE));
            transitionSelectExpression->addChild(new AST("ingress_port", NT_NONE));
        }
        else{ //Egress
            AST *extractExpression = new AST(NULL, NT_METHOD_CALL_STATEMENT);
            extractExpression->addChild(new AST(".", NT_EXPRESSION));
            extractExpression->children[0]->addChild(new AST(packetParamName, NT_EXPRESSION));
            extractExpression->children[0]->addChild(new AST("extract", NT_NONE));
            extractExpression->addChild(NULL);
            extractExpression->addChild(new AST(NULL, NT_ARGUMENT_LIST));
            extractExpression->children[2]->addChild(new AST(NULL, NT_ARGUMENT));
            extractExpression->children[2]->children[0]->addChild(new AST(EG_INTRINSIC_MD_PARAM_NAME, NT_EXPRESSION));

            if(startState->getChildNT(NT_PARSER_STATEMENT_LIST) == NULL){
                startState->children[1] = new AST(NULL, NT_PARSER_STATEMENT_LIST);
                startState->children[1]->addChild(extractExpression);
            }

            transitionSelectExpression->addChild(new AST(EG_INTRINSIC_MD_PARAM_NAME, NT_NONE));
            transitionSelectExpression->addChild(new AST("egress_port", NT_NONE));
        }

        AST *selectExp = startState->getChildNT(NT_SELECT_EXPRESSION);
        Assert(selectExp != NULL);
        AST *expressionList = selectExp->getChildNT(NT_EXPRESSION_LIST);
        Assert(expressionList != NULL && expressionList->children.size() == 1);
        delete expressionList->children[0];
        expressionList->children[0] = transitionSelectExpression;

        //Add packet-out handling in ingress parser
        if(cpuPort > 0 && parserParam == 0){
            for(size_t s = 0; s < parser->getChildNT(NT_PARSER_STATE_LIST)->children.size(); s++){
                AST *state = parser->getChildNT(NT_PARSER_STATE_LIST)->children[s];
                if(state->value == "start"){
                    char buffer[128];

                    AST *transition = state->getChildNT(NT_SELECT_EXPRESSION);
                    Assert(transition != NULL);
                    AST *newSelectCase = new AST("VirtParsePacketOut", NT_SELECT_CASE);
                    sprintf(buffer, "%d", cpuPort);
                    newSelectCase->addChild(new AST(std::string(buffer), NT_EXPRESSION));
                    transition->getChildNT(NT_SELECT_CASE_LIST)->addChild(newSelectCase);
                
                    AST *newState = new AST("VirtParsePacketOut", NT_PARSER_STATE);
                    newState->addChild(NULL); //optAnnotations
                    newState->addChild(new AST(NULL, NT_PARSER_STATEMENT_LIST));
                    newState->addChild(new AST("select", NT_SELECT_EXPRESSION));
                    
                    AST *tempVarDec = new AST("VirtTempPacketOutVar", NT_VARIABLE_DECLARATION);
                    tempVarDec->addChild(NULL);
                    tempVarDec->addChild(new AST(VIRT_PACKET_OUT_HEADER_TYPE, NT_TYPE_NAME));
                    tempVarDec->addChild(NULL);
                    AST *extractPacketOutHeader = new AST(NULL, NT_METHOD_CALL_STATEMENT);
                    extractPacketOutHeader->addChild(new AST(".", NT_NONE));
                    extractPacketOutHeader->children[0]->addChild(new AST(packetParamName, NT_EXPRESSION));
                    extractPacketOutHeader->children[0]->addChild(new AST("extract", NT_NONE));
                    extractPacketOutHeader->addChild(NULL);
                    extractPacketOutHeader->addChild(new AST(NULL, NT_ARGUMENT_LIST));
                    extractPacketOutHeader->children[2]->addChild(new AST(NULL, NT_ARGUMENT));
                    extractPacketOutHeader->children[2]->children[0]->addChild(new AST("VirtTempPacketOutVar", NT_EXPRESSION));
                    newState->children[1]->addChild(tempVarDec);
                    newState->children[1]->addChild(extractPacketOutHeader);
                
                    parser->getChildNT(NT_PARSER_STATE_LIST)->children.insert(parser->getChildNT(NT_PARSER_STATE_LIST)->children.begin() + s + 1, newState);
                    s++;

                    newState->getChildNT(NT_SELECT_EXPRESSION)->addChild(new AST(NULL, NT_EXPRESSION_LIST));
                    AST *transitionKey = new AST(".", NT_EXPRESSION);
                    transitionKey->addChild(new AST("VirtTempPacketOutVar", NT_EXPRESSION));
                    transitionKey->children[0]->setProgramType(VIRT_PACKET_OUT_HEADER_TYPE);
                    transitionKey->addChild(new AST(VIRT_PACKET_OUT_HEADER_PROG_ID_FIELD_NAME, NT_NONE));
                    newState->getChildNT(NT_SELECT_EXPRESSION)->getChildNT(NT_EXPRESSION_LIST)->addChild(transitionKey);

                    newState->getChildNT(NT_SELECT_EXPRESSION)->addChild(new AST(NULL, NT_SELECT_CASE_LIST));
                    for(int p = 0; p < (int) programs.size(); p++){
                        sprintf(buffer, "P%d_start", p + 1);
                        AST *sc = new AST(std::string(buffer), NT_SELECT_CASE);
                        sprintf(buffer, "%d", p + 1);
                        sc->addChild(new AST(std::string(buffer), NT_EXPRESSION));
                        newState->getChildNT(NT_SELECT_EXPRESSION)->getChildNT(NT_SELECT_CASE_LIST)->addChild(sc);
                    }
                }
            }
        }

        pCounter++;

        for(AST* n: newNodes)
            nodesToAdd.push_back(n);
    }

    //Controls
    const std::vector<int> controlParams = {1, 2, 4, 5};
    const std::vector<std::string> controlNames = {"VirtIngress", "VirtIngressDeparser", "VirtEgress", "VirtEgressDeparser"};
    int controlPipeN = 0;
    for(int controlP : controlParams){
        std::vector<AST*> elements;
        std::vector<std::string> elementNames;
        for(AST* pipeline : pipelines)
            elementNames.push_back(pipeline->getChildNT(NT_ARGUMENT_LIST)->children[controlP]->children[0]->value);

        for(int p = 0; p < (int) programs.size(); p++){
            int i = 0;
            while(i < (int) programs[p]->children.size() && (programs[p]->children[i]->nType != NT_CONTROL_DECLARATION || programs[p]->children[i]->children[0]->value != elementNames[p]))
                i++;
            if(i == (int) programs[p]->children.size()){
                fprintf(stderr, "TofinoMerge: Could not find \"%s\" Control declaration\n", elementNames[p].c_str());
                return false;
            }
            elements.push_back(programs[p]->children[i]);
        }

        std::vector<AST*> newNodes = AST::MergeControls(programs, elements, headersDec, controlNames[controlPipeN], virtualSwitchPorts);
        if(newNodes.size() == 0)
            return false;

        AST* mergedControl = newNodes[newNodes.size() - 1];
        for(AST *param : mergedControl->children[0]->children[2]->children){
            std::string key = AST::RemoveMergePrefix(param->children[2]->value);
            
            auto it = ParamNames.find(key);
            if(it != ParamNames.end()){
                if(mergedControl->getChildNT(NT_CONTROL_LOCAL_DEC_LIST) != NULL){
                    mergedControl->getChildNT(NT_CONTROL_LOCAL_DEC_LIST)->SubstituteIdentifierWithType(param->value, it->second, param->children[2]->value);
                }
                if(mergedControl->getChildNT(NT_STAT_OR_DEC_LIST) != NULL)
                    mergedControl->getChildNT(NT_STAT_OR_DEC_LIST)->SubstituteIdentifierWithType(param->value, it->second, param->children[2]->value);
                param->value = it->second;
            }
        }

        //Add egress port check
        if(controlPipeN == 0){
            if(mergedControl->children[2] == NULL)
                mergedControl->children[2] = new AST(NULL, NT_CONTROL_LOCAL_DEC_LIST);
            if(mergedControl->children[3] == NULL)
                mergedControl->children[3] = new AST(NULL, NT_STAT_OR_DEC_LIST);
            AST *controlLDs = mergedControl->children[2];

            AST *actionOK= new AST("VirtOutPortOK", NT_ACTION_DECLARATION);
            controlLDs->addChild(actionOK);
            actionOK->addChild(new AST(NULL, NT_ANNOTATION_LIST));
            actionOK->children[0]->addChild(new AST("hidden", NT_ANNOTATION));
            actionOK->children[0]->children[0]->addChild(NULL);
            actionOK->addChild(NULL); //parameterList
            actionOK->addChild(NULL);

            AST *actionSendToCPU;
            if(cpuPort > 0){
                actionSendToCPU = new AST("VirtSendToCPU", NT_ACTION_DECLARATION);
                controlLDs->addChild(actionSendToCPU);
                actionSendToCPU->addChild(new AST(NULL, NT_ANNOTATION_LIST));
                actionSendToCPU->children[0]->addChild(new AST("hidden", NT_ANNOTATION));
                actionSendToCPU->children[0]->children[0]->addChild(NULL);
                actionSendToCPU->addChild(new AST(NULL, NT_PARAMETER_LIST));
                actionSendToCPU->children[1]->addChild(new AST("pID", NT_PARAMETER));
                actionSendToCPU->children[1]->children[0]->addChild(NULL); //annotations
                actionSendToCPU->children[1]->children[0]->addChild(NULL); //direction
                actionSendToCPU->children[1]->children[0]->addChild(new AST("bit", NT_BASE_TYPE));
                actionSendToCPU->children[1]->children[0]->children[2]->addChild(new AST("8", NT_LITERAL));
                AST *actionSendToCPUBlock = new AST(NULL, NT_STAT_OR_DEC_LIST);
                actionSendToCPU->addChild(actionSendToCPUBlock);
                const std::string headersParamName = mergedControl->getChildNT(NT_CONTROL_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[0]->value;
                actionSendToCPUBlock->addChild(new AST(NULL, NT_METHOD_CALL_STATEMENT));
                actionSendToCPUBlock->children[0]->addChild(new AST(".", NT_NONE));
                actionSendToCPUBlock->children[0]->children[0]->addChild(new AST(".", NT_EXPRESSION));
                actionSendToCPUBlock->children[0]->children[0]->children[0]->addChild(new AST(headersParamName, NT_EXPRESSION));
                actionSendToCPUBlock->children[0]->children[0]->children[0]->addChild(new AST(std::string(VIRT_PACKET_IN_HEADER_NAME), NT_NONE));
                actionSendToCPUBlock->children[0]->children[0]->addChild(new AST("setValid", NT_NONE));
                actionSendToCPUBlock->children[0]->addChild(NULL);
                actionSendToCPUBlock->children[0]->addChild(NULL);
                actionSendToCPUBlock->addChild(new AST("==", NT_ASSIGN_STATEMENT));
                actionSendToCPUBlock->children[1]->addChild(new AST(".", NT_NONE));
                actionSendToCPUBlock->children[1]->children[0]->addChild(new AST(".", NT_NONE));
                actionSendToCPUBlock->children[1]->children[0]->children[0]->addChild(new AST(headersParamName, NT_NONE));
                actionSendToCPUBlock->children[1]->children[0]->children[0]->addChild(new AST(std::string(VIRT_PACKET_IN_HEADER_NAME), NT_NONE));
                actionSendToCPUBlock->children[1]->children[0]->addChild(new AST(std::string(VIRT_PACKET_IN_HEADER_PROG_ID_FIELD_NAME), NT_NONE));
                actionSendToCPUBlock->children[1]->addChild(new AST("pID", NT_EXPRESSION));
            }

            AST *actionDropUnmappedPort = new AST("VirtOutPortWrong", NT_ACTION_DECLARATION);
            controlLDs->addChild(actionDropUnmappedPort);
            actionDropUnmappedPort->addChild(new AST(NULL, NT_ANNOTATION_LIST));
            actionDropUnmappedPort->children[0]->addChild(new AST("hidden", NT_ANNOTATION));
            actionDropUnmappedPort->children[0]->children[0]->addChild(NULL);
            actionDropUnmappedPort->addChild(NULL); //parameterList
            actionDropUnmappedPort->addChild(new AST(NULL, NT_STAT_OR_DEC_LIST));
            AST *assignStatement = new AST("=", NT_ASSIGN_STATEMENT);
            assignStatement->addChild(new AST(".", NT_NONE));
            assignStatement->children[0]->addChild(new AST(IG_INTRINSIC_MD_DEPARSER_PARAM_NAME, NT_NONE));
            assignStatement->children[0]->addChild(new AST("drop_ctl", NT_NONE));
            assignStatement->addChild(new AST("1", NT_EXPRESSION));
            actionDropUnmappedPort->children[2]->addChild(assignStatement);

            AST *tableProgramID = new AST("VirtTableCheckProgramID", NT_TABLE_DECLARATION);
            controlLDs->addChild(tableProgramID);
            tableProgramID->addChild(new AST(NULL, NT_ANNOTATION_LIST));
            tableProgramID->children[0]->addChild(new AST("hidden", NT_ANNOTATION));
            tableProgramID->children[0]->children[0]->addChild(NULL);
            AST *tablePL = new AST(NULL, NT_TABLE_PROPERTY_LIST);
            tableProgramID->addChild(tablePL);
            tablePL->addChild(new AST("key", NT_TABLE_PROPERTY));
            AST *key = new AST("exact", NT_KEY_ELEMENT);
            key->addChild(new AST(".", NT_EXPRESSION));
            key->children[0]->addChild(new AST(IG_INTRINSIC_MD_TM_PARAM_NAME, NT_EXPRESSION));
            key->children[0]->addChild(new AST("ucast_egress_port", NT_NONE));
            key->addChild(NULL); //optAnnotations
            AST *key2 = new AST("exact", NT_KEY_ELEMENT);
            key2->addChild(new AST(".", NT_EXPRESSION));
            key2->children[0]->addChild(new AST(METADATA_NAME, NT_EXPRESSION));
            key2->children[0]->addChild(new AST(METADATA_PROG_ID_FIELD_NAME, NT_NONE));
            key2->addChild(NULL); //optAnnotations
            tablePL->children[0]->addChild(new AST(NULL, NT_KEY_ELEMENT_LIST));
            tablePL->children[0]->children[0]->addChild(key);
            tablePL->children[0]->children[0]->addChildStart(key2);
            tablePL->addChild(new AST("actions", NT_TABLE_PROPERTY));
            tablePL->children[1]->addChild(new AST(NULL, NT_ACTION_LIST));
            tablePL->children[1]->children[0]->addChild(new AST(actionOK->value, NT_ACTION_REF));
            if(cpuPort > 0)
                tablePL->children[1]->children[0]->addChild(new AST(actionSendToCPU->value, NT_ACTION_REF));
            tablePL->children[1]->children[0]->addChild(new AST(actionDropUnmappedPort->value, NT_ACTION_REF));
            tablePL->children[1]->children[0]->children[0]->addChild(NULL);
            tablePL->children[1]->children[0]->children[1]->addChild(NULL);
            if(cpuPort > 0)
                tablePL->children[1]->children[0]->children[2]->addChild(NULL);

            tablePL->addChild(new AST("const default_action", NT_TABLE_PROPERTY));
            tablePL->children[2]->addChild(new AST(actionDropUnmappedPort->value, NT_EXPRESSION));

            char buffer[128];
            tablePL->addChild(new AST("const entries", NT_TABLE_PROPERTY));
            tablePL->children[3]->addChild(new AST(NULL, NT_ENTRIES_LIST));
            int tSize = 0;
            for(int vsN = 0; vsN < (int) virtualSwitchPorts.size(); vsN++){
                tSize = tSize + ((int) virtualSwitchPorts[vsN].size());
                for(int port : virtualSwitchPorts[vsN]){
                    AST *entry = new AST(NULL, NT_ENTRY);
                    tablePL->children[3]->children[0]->addChild(entry);
                    entry->addChild(new AST(NULL, NT_TUPLE_KEYSET_EXPRESSION));
                    entry->children[0]->addChild(new AST(NULL, NT_SIMPLE_EXPRESSION_LIST));
                    sprintf(buffer, "%d", vsN + 1);
                    entry->children[0]->children[0]->addChild(new AST(buffer, NT_EXPRESSION));
                    sprintf(buffer, "%d", port);
                    entry->children[0]->children[0]->addChild(new AST(buffer, NT_EXPRESSION));
                    entry->addChild(new AST(actionOK->value, NT_ACTION_REF));
                    entry->children[1]->addChild(NULL);
                    entry->addChild(NULL); //optAnnotations
                }

                //CPU-Port
                if(cpuPort > 0){
                    tSize++;
                    AST *entry = new AST(NULL, NT_ENTRY);
                    tablePL->children[3]->children[0]->addChild(entry);
                    entry->addChild(new AST(NULL, NT_TUPLE_KEYSET_EXPRESSION));
                    entry->children[0]->addChild(new AST(NULL, NT_SIMPLE_EXPRESSION_LIST));
                    sprintf(buffer, "%d", vsN + 1);
                    entry->children[0]->children[0]->addChild(new AST(buffer, NT_EXPRESSION));
                    sprintf(buffer, "%s", "CPU_PORT");
                    entry->children[0]->children[0]->addChild(new AST(buffer, NT_EXPRESSION));
                    entry->addChild(new AST(actionSendToCPU->value, NT_ACTION_REF));
                    sprintf(buffer, "%d", vsN + 1);
                    entry->children[1]->addChild(new AST(std::string(buffer), NT_EXPRESSION));
                    entry->addChild(NULL); //optAnnotations
                }
            }

            sprintf(buffer, "%d", tSize);
            tablePL->addChild(new AST("size", NT_TABLE_PROPERTY));
            tablePL->children[4]->addChild(new AST(buffer, NT_LITERAL));

            AST* tableApplyStatement = new AST(NULL, NT_METHOD_CALL_STATEMENT);
            mergedControl->children[3]->addChild(tableApplyStatement);
            tableApplyStatement->addChild(new AST(".", NT_NONE));
            tableApplyStatement->children[0]->addChild(new AST("VirtTableCheckProgramID", NT_NONE));
            tableApplyStatement->children[0]->addChild(new AST("apply", NT_NONE));
            tableApplyStatement->addChild(NULL);
            tableApplyStatement->addChild(NULL);

            mergedControl->RemoveSwitchStatements();
        }
        else if(controlPipeN == 1){
            TofinoMerge::RemoveSwitchCaseEmitsFromDeparser(mergedControl);

            //Add virtPacketIn Header emit
            if(cpuPort > 0){
                const std::string packetOutParamName = mergedControl->getChildNT(NT_CONTROL_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[0]->value;
                const std::string headersParamName = mergedControl->getChildNT(NT_CONTROL_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[1]->value;
                AST *virtPacketInEmit = new AST(NULL, NT_METHOD_CALL_STATEMENT);
                virtPacketInEmit->addChild(new AST(".", NT_NONE));
                virtPacketInEmit->children[0]->addChild(new AST(packetOutParamName, NT_NONE));
                virtPacketInEmit->children[0]->children[0]->programType = mergedControl->getChildNT(NT_CONTROL_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST)->children[0]->getChildNT(NT_TYPE_NAME)->value;
                virtPacketInEmit->children[0]->addChild(new AST("emit", NT_NONE));
                virtPacketInEmit->addChild(NULL);
                virtPacketInEmit->addChild(new AST(NULL, NT_ARGUMENT_LIST));
                virtPacketInEmit->children[2]->addChild(new AST(NULL, NT_ARGUMENT));
                virtPacketInEmit->children[2]->children[0]->addChild(new AST(".", NT_EXPRESSION));
                virtPacketInEmit->children[2]->children[0]->children[0]->addChild(new AST(headersParamName, NT_NONE));
                virtPacketInEmit->children[2]->children[0]->children[0]->addChild(new AST(std::string(VIRT_PACKET_IN_HEADER_NAME), NT_NONE));
                Assert(mergedControl->getChildNT(NT_STAT_OR_DEC_LIST) != NULL);
                mergedControl->getChildNT(NT_STAT_OR_DEC_LIST)->addChildStart(virtPacketInEmit);
            }
        }

        controlPipeN++;

        for(AST* n: newNodes)
            nodesToAdd.push_back(n);
    }

    //Construct new pipeline
    AST *newPipe = pipelines[pipelines.size() - 1]->Clone();
    newPipe->value = SINGLE_PIPELINE_NAME;
    newPipe->getChildNT(NT_TYPE_NAME)->value = "Pipeline";
    for(int i = 0; i < (int) parserParams.size(); i++)
        newPipe->getChildNT(NT_ARGUMENT_LIST)->children[parserParams[i]]->children[0]->value = parserNames[i];
    for(int i = 0; i < (int) controlParams.size(); i++)
        newPipe->getChildNT(NT_ARGUMENT_LIST)->children[controlParams[i]]->children[0]->value = controlNames[i];
    nodesToAdd.push_back(newPipe);

    for(int i = 0; i < (int) programs.size(); i++){
        int cc = 0;
        while(cc < (int) programs[i]->children.size()){
            if(programs[i]->children[cc] == pipelines[i]){
                programs[i]->children.erase(programs[i]->children.begin() + cc);
                cc--;
            }
            cc++;
        }
        delete pipelines[i];
    }

    for(AST* n : nodesToAdd)
        programs[programs.size() - 1]->addChild(n);

    return true;
}

bool TofinoMerge::RemoveSwitchInstantiation(AST *ast){
    int i = 0;
    while(i < (int) ast->children.size() && (ast->children[i]->nType != NT_INSTANTIATION || ast->children[i]->value != "main"))
        i++;
    if(i == (int) ast->children.size()){
        fprintf(stderr, "TofinoMerge: Could not find \"main\" Switch instantiation\n");
        return false;
    }

    delete ast->children[i];
    ast->children.erase(ast->children.begin() + i);
    return true;
}

bool TofinoMerge::GetPipelines(AST *ast, std::vector<AST*> *Pipelines){
    int i = 0;
    while(i < (int) ast->children.size() && (ast->children[i]->nType != NT_INSTANTIATION || ast->children[i]->value != "main"))
        i++;
    if(i == (int) ast->children.size()){
        fprintf(stderr, "TofinoMerge: Could not find \"main\" Switch instantiation\n");
        return false;
    }

    for(AST *argument : ast->children[i]->getChildNT(NT_ARGUMENT_LIST)->children){
        std::string instName = argument->children[0]->value;
        int j = 0;
        while(j < (int) ast->children.size() && (ast->children[j]->nType != NT_INSTANTIATION || ast->children[j]->value != instName))
            j++;
        if(j == (int) ast->children.size()){
            fprintf(stderr, "TofinoMerge: Could not find \"%s\" Pipeline instantiation\n", instName.c_str());
            return false;
        }

        if(ast->children[j]->getChildNT(NT_ARGUMENT_LIST) == NULL || ast->children[j]->getChildNT(NT_ARGUMENT_LIST)->children.size() != 6){
            fprintf(stderr, "TofinoMerge: \"%s\" Pipeline instantiation does not have 6 arguments\n", instName.c_str());
            return false;
        }

        Pipelines->push_back(ast->children[j]);
    }

    return true;
}

void TofinoMerge::RemoveSwitchCaseEmitsFromDeparser(AST* deparser){
    //TODO: Might need to prevent a program from emiting the same header twice
    //TODO: Verify emit order

    AST *parameterList = deparser->getChildNT(NT_CONTROL_TYPE_DECLARATION)->getChildNT(NT_PARAMETER_LIST);
    Assert(parameterList != NULL);
    Assert(parameterList->children.size() >= 3);

    const std::string PacketOutParamName = parameterList->children[0]->value;
    const std::string HeadersParamName = parameterList->children[1]->value;

    std::vector<AST*> programIDSwitchStatements;

    Assert(deparser != NULL);
    AST *applyStatOrDecList = deparser->getChildNT(NT_STAT_OR_DEC_LIST);
    Assert(applyStatOrDecList != NULL);

    for(AST *statement : applyStatOrDecList->children){
        if(statement->getNodeType() == NT_SWITCH_STATEMENT && statement->children[0]->value == TEMP_PROGRAM_CODE)
            programIDSwitchStatements.push_back(statement);
    }

    std::vector<AST*> uniqueEmitStatements;
    std::unordered_map<std::string, size_t> emittedHeaders;
    for(AST *switchCaseStatement : programIDSwitchStatements){
        AST *SCL = switchCaseStatement->getChildNT(NT_SWITCH_CASE_LIST);
        for(size_t si = 0; si < SCL->children.size(); si++){
            AST *switchCase = SCL->children[si];
            
            //Check for fall through
            if(switchCase->children.size() == 1){
                size_t sj = si + 1;
                while(sj < SCL->children.size() && SCL->children[sj]->children.size() == 1)
                    sj++;
                Assert(sj < SCL->children.size());
                switchCase->addChild(SCL->children[sj]->children[1]->Clone());
            }

            AST *caseSODList = switchCase->getChildNT(NT_STAT_OR_DEC_LIST);
            size_t i = 0;
            while(i < caseSODList->children.size()){
                AST *curChild = caseSODList->children[i];
                if(curChild->getNodeType() == NT_METHOD_CALL_STATEMENT){
                    if(curChild->children[0]->value == "." && curChild->children[0]->children[0]->value == PacketOutParamName && curChild->children[0]->children[1]->value == "emit"){
                        bool invalidEmit = false;
                        AST *argumentList = curChild->getChildNT(NT_ARGUMENT_LIST);
                        if(argumentList != NULL && argumentList->children.size() == 1){
                            if(argumentList->children[0]->children[0]->value == "."){
                                if(argumentList->children[0]->children[0]->children[0]->value == HeadersParamName){
                                    const std::string emittedHeader = argumentList->children[0]->children[0]->children[1]->value;
                                    if(emittedHeaders.find(emittedHeader) == emittedHeaders.end()){
                                        emittedHeaders.insert(std::make_pair(emittedHeader, emittedHeaders.size()));
                                        uniqueEmitStatements.push_back(curChild->Clone());
                                    }
                                    else{
                                        size_t pos = emittedHeaders.find(emittedHeader)->second;
                                        AST *aux = uniqueEmitStatements[pos];
                                        for(size_t x = pos + 1; x < uniqueEmitStatements.size(); x++){
                                            uniqueEmitStatements[x-1] = uniqueEmitStatements[x];
                                        }
                                        uniqueEmitStatements[uniqueEmitStatements.size() - 1] = aux;
                                        for(auto& it : emittedHeaders)
                                            if(it.second > pos)
                                                it.second = it.second - 1;
                                        emittedHeaders.find(emittedHeader)->second = uniqueEmitStatements.size() - 1;
                                    }
                                }
                                else
                                    invalidEmit = true;
                            }
                            else
                                invalidEmit = true;
                        }
                        else
                            invalidEmit = true;

                        if(!invalidEmit){
                            delete curChild;
                            caseSODList->children.erase(caseSODList->children.begin() + i);
                            i--;
                        }
                    }
                }
                i++;
            }
        }

        for(AST *as : uniqueEmitStatements)
            applyStatOrDecList->addChild(as);

        //AST::RemoveEqualSwitchCases(switchCaseStatement);
    }
}
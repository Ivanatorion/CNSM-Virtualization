#include "../include/pcheaders.h"
#include "../include/IPLBase.hpp"
#include "../include/Log.h"
#include "../include/AST.h"
#include "../include/ASTParser.h"

#define STATE_DEPTH_LIMIT 10000

void ASTParser::CheckParsersStates(AST *parser, const std::string& programName){
    const std::string parserName = parser->getChildNT(NT_PARSER_TYPE_DECLARATION)->value;
    
    Log::MSG("Parser \"" + parserName + "\" (" + programName + ")");
    Log::Push();

    AST *StateList = parser->getChildNT(NT_PARSER_STATE_LIST);
    if(StateList == NULL || StateList->FindStateInList("start") == NULL){
        fprintf(stderr, "Error: Parser \"%s\" in program \"%s\" does not have a start state\n", parserName.c_str(), programName.c_str());
        exit(1);
    }

    ASTParser::OrderParserStates(StateList, parser->value, programName);

    Log::Pop();
}

void ASTParser::OrderParserStates(AST *StateList, const std::string& parserName, const std::string& programName){
    std::unordered_map<AST*, int> distances;
    std::unordered_map<AST*, int> nInEdges;
    std::vector<AST*> queue;
    std::unordered_set<AST*> closed;
    std::vector<AST*> ordered;
    for(AST *state : StateList->children){
        distances.emplace(state, state->value == "start" ? 0 : STATE_DEPTH_LIMIT);
        nInEdges.emplace(state, 0);
    }

    queue.push_back(StateList->FindStateInList("start"));
    while(queue.size() > 0){
        AST *s = queue[0];
        queue.erase(queue.begin());
        if(closed.find(s) == closed.end()){
            closed.insert(s);
            ordered.push_back(s);
            const int sDistance = distances.find(s)->second;

            if(s->getChildNT(NT_NAME_STATE_EXPRESSION) != NULL){
                if(s->getChildNT(NT_NAME_STATE_EXPRESSION)->value != "accept" && s->getChildNT(NT_NAME_STATE_EXPRESSION)->value != "reject"){
                    AST *nextS = StateList->FindStateInList(s->getChildNT(NT_NAME_STATE_EXPRESSION)->value);
                    if(nextS == NULL){
                        fprintf(stderr, "Error: State \"%s\" not found in parser \"%s\" (%s - Line %d)\n", s->getChildNT(NT_NAME_STATE_EXPRESSION)->str(), parserName.c_str(), programName.c_str(), s->getChildNT(NT_NAME_STATE_EXPRESSION)->lineNumber);
                        exit(1);
                    }
                    nInEdges.find(nextS)->second = nInEdges.find(nextS)->second + 1;

                    if(closed.find(nextS) == closed.end()){
                        queue.push_back(nextS);
                        if(distances.find(nextS)->second > sDistance + 1)
                            distances.find(nextS)->second = sDistance + 1;
                    }
                }
            }
            else{
                for(AST *sc : s->getChildNT(NT_SELECT_EXPRESSION)->children[1]->children){
                    if(sc->value != "accept" && sc->value != "reject"){
                        AST *nextS = StateList->FindStateInList(sc->value);
                        if(nextS == NULL){
                            fprintf(stderr, "Error: State \"%s\" not found in parser \"%s\" (%s - Line %d)\n", sc->str(), parserName.c_str(), programName.c_str(), sc->lineNumber);
                            exit(1);
                        }
                        nInEdges.find(nextS)->second = nInEdges.find(nextS)->second + 1;

                        if(closed.find(nextS) == closed.end()){
                            queue.push_back(nextS);
                            if(distances.find(nextS)->second > sDistance + 1)
                                distances.find(nextS)->second = sDistance + 1;
                        }
                    }
                } 
            }
        }
    }

    for(auto d : distances){
        if(d.second == STATE_DEPTH_LIMIT){
            Log::MSG("Removed unreachable state \"" + d.first->value + "\"");
            delete d.first;
        }
    }

    StateList->children.clear();
    for(AST* s : ordered)
        StateList->addChild(s);

    int itr = 0;
    while(itr < (int) StateList->children.size()){
        AST *state = StateList->children[itr];
        AST *simpleTransitionStatement = state->getChildNT(NT_NAME_STATE_EXPRESSION);
        if(simpleTransitionStatement != NULL && (simpleTransitionStatement->value != "accept" && simpleTransitionStatement->value != "reject")){
            AST *nextS = StateList->FindStateInList(simpleTransitionStatement->value);
            Assert(nextS != NULL);
            if(nInEdges.find(nextS)->second == 1){
                if(state->children[1] == NULL)
                    state->children[1] = NULL_CLONE(nextS->children[1]);
                else if(nextS->children[1] != NULL){
                    for(AST *statement : nextS->children[1]->children)
                        state->children[1]->addChild(NULL_CLONE(statement));
                }

                delete simpleTransitionStatement;
                state->children[2] = NULL_CLONE(nextS->children[2]);

                int itr2 = itr + 1;
                while(itr2 < (int) StateList->children.size() && StateList->children[itr2] != nextS)
                    itr2++;
                Assert(itr2 < (int) StateList->children.size());
                
                StateList->children.erase(StateList->children.begin() + itr2);
                delete nextS;
                itr--;
            }
        }
        itr++;
    }
}

void ASTParser::WriteParserDOT(AST *parser, const std::string& fileName){
    char buffer[256];

    if(parser->nType != NT_PARSER_DECLARATION)
        return;

    FILE *fp = fopen(fileName.c_str(), "w");
    Assert(fp != NULL);
    if(!fp)
        return;

    std::unordered_map<std::string, std::set<std::string>> transitions; 

    for(AST* state : parser->getChildNT(NT_PARSER_STATE_LIST)->children){
        if(state->getChildNT(NT_NAME_STATE_EXPRESSION) != NULL){
            transitions.emplace(state->value, std::set<std::string>({state->getChildNT(NT_NAME_STATE_EXPRESSION)->value}));
        }
        else{
            transitions.emplace(state->value, std::set<std::string>());
            AST* SCL = state->getChildNT(NT_SELECT_EXPRESSION)->getChildNT(NT_SELECT_CASE_LIST);
            for(AST *nextState : SCL->children)
                transitions.find(state->value)->second.emplace(nextState->value);
        }
    }

    fprintf(fp, "digraph AST {\n");
    for(AST *state : parser->getChildNT(NT_PARSER_STATE_LIST)->children){
        sprintf(buffer, "%s", state->value.c_str());
        fprintf(fp, "\"%s\" [ shape=record, style=\"filled\", fillcolor=cornsilk, label=\"%s\" ];\n", buffer, buffer);
    }
    fprintf(fp, "accept [ shape=record, style=\"filled\", fillcolor=cornsilk, label=\"accept\" ];\n");
    fprintf(fp, "reject [ shape=record, style=\"filled\", fillcolor=cornsilk, label=\"reject\" ];\n");
    for(AST *state : parser->getChildNT(NT_PARSER_STATE_LIST)->children){
        sprintf(buffer, "%s", state->value.c_str());
        for(const std::string& transition : transitions.find(state->value)->second){
            char buffer2[256];
            sprintf(buffer2, "%s", transition.c_str());
            fprintf(fp, "\"%s\" -> \"%s\"\n", buffer, buffer2);
        }
    }
    fprintf(fp, "}\n");
    fclose(fp);
}
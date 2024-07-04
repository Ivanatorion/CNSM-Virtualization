#ifndef TOFINO_MERGE_H
#define TOFINO_MERGE_H

class TofinoMerge {
    public:
        static bool MergeTofino(const std::vector<AST*> programs, const std::vector<AST*>& headersDec, const std::vector<std::set<int>>& virtualSwitchPorts, const int cpuPort);
    private:
        static bool RemoveSwitchInstantiation(AST *ast);
        static bool GetPipelines(AST *ast, std::vector<AST*> *Pipelines);
        static bool MergePipelines(const std::vector<AST*>& programs, const std::vector<AST*>& pipelines, const std::vector<AST*>& headersDec, const std::vector<std::set<int>>& virtualSwitchPorts, const int cpuPort);

        static bool RemoveIngressPortMetadataExtractions(const std::string& programName, AST *statelist, AST *state, const std::string packetInName, const std::string parameterName, bool intrinsicMDportMD);
        static bool RemoveEgressPortMetadataExtractions(const std::string& programName, AST *statelist, AST *state, const std::string packetInName, const std::string parameterName);

        static void RemoveSwitchCaseEmitsFromDeparser(AST *deparser);
};

#endif

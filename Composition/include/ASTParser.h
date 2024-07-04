#ifndef AST_PARSER_H
#define AST_PARSER_H

class ASTParser {
    public:
        static void CheckParsersStates(AST *parser, const std::string& programName);
        static void WriteParserDOT(AST *parser, const std::string& fileName);

    private:
        static void OrderParserStates(AST *StateList, const std::string& parserName, const std::string& programName);
};

#endif
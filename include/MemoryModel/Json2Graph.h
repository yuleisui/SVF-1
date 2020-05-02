#ifndef INCLUDE_MEMORYMODEL_JSON2GRAPH_H_
#define INCLUDE_MEMORYMODEL_JSON2GRAPH_H_

#include "Graphs/ICFG.h"
#include "Graphs/PAG.h"
#include "Graphs/PTACallGraph.h"
#include "llvm/Support/JSON.h"

class PAGBuilderFromFile {

private:
    PAG* pag;
    std::string file;
public:
    /// Constructor
    PAGBuilderFromFile(std::string f) :
        pag(PAG::getPAG(true)), file(f) {
    }
    /// Destructor
    ~PAGBuilderFromFile() {
    }

    /// Return PAG
    PAG* getPAG() const {
        return pag;
    }

    /// Return file name
    std::string getFileName() const {
        return file;
    }

    //start building pag from ICFG json file
    PAG* build();

    //add pag nodes
    void addNode(NodeID ID, std::string node_type, const char*  str_val);

    // Add edges
    void addEdge(NodeID nodeSrc, NodeID nodeDst, Size_t offset,
                 std::string edge);
};


class CallGraphBuilderFromFile{

private:
    PTACallGraph* ptaCallGraph;
    std::string file;

public: 
    //Constructor
    CallGraphBuilderFromFile(std::string f){
        this->file = f;
    }

    // Destructor
    ~CallGraphBuilderFromFile() {}

    // Return ICFG
    PTACallGraph* getPATCallGraph() const{
        return ptaCallGraph;
    }

    std::string getFileName() const {
        return file;
    }
    //Start building
    PTACallGraph* build();

    void addNode(const SVFFunction* svf_function);

    void addEdge();
};

class ICFGBuilderFromFile{

private:
    ICFG* icfg;
    std::string file;
    PAG* pag;

public: 
    //Constructor
    ICFGBuilderFromFile(std::string f, PAG* pag){
        this->pag = pag;
    }

    // Destructor
    ~ICFGBuilderFromFile() {}

    // Return ICFG
    ICFG* getICFG() const{
        return icfg;
    }

    std::string getFileName() const {
        return file;
    }

    PAG getPAG() const{
        return pag;
    }
    //Start building
    ICFG* build();

    void addNode(NodeID nodeID, std::string nodeType, llvm::json::Array* pagEdges);

    void addEdge(llvm::json::Object* edge_obj);
};

#endif
#ifndef SVFPAG_H_
#define SVFPAG_H_

#include "Graphs/PAGEdge.h"
#include "Graphs/PAGNode.h"
#include "Util/SVFUtil.h"
#include "Graphs/ICFG.h"

class SVFPAG : public PAG{
    public:
    SVFPAG(bool buildFromFile):PAG(buildFromFile){}
    inline NodeID addValNodeFromFile(const std::string val, PAGNode* node, NodeID i){
        return addNode(node,i);
    }
     inline NodeID addObjNodeFromFile(const std::string str_val, PAGNode* node, NodeID i){
        return addNode(node,i);
    }

     inline NodeID addRetNodeFromFile(const std::string str_val, PAGNode *node, NodeID i){
        return addNode(node, i);
    }

    inline NodeID addVarargNodeFromFile(const std::string val, PAGNode * node, NodeID i){
        return addNode(node,i);
    }
    
};

#endif
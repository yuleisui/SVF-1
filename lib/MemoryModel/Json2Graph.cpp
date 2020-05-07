#include "MemoryModel/Json2Graph.h"
#include <fstream>	// for ICFGBuilderFromFile
#include <string>	// for ICFGBuilderFromFile
#include <sstream>	// for ICFGBuilderFromFile
#include "llvm/Support/JSON.h"
#include "MemoryModel/SVFPAG.h"

using namespace std;
using namespace SVFUtil;

static u32_t gepNodeNumIndex = 100000;
SVFPAG* svf_pag;

void PAGBuilderFromFile::addNode(NodeID ID, std::string node_type, const std::string& str_val){
	if(node_type=="DummyValNode"){
		svf_pag->addDummyValNode(ID);
	}
	else if(node_type=="FIObjNode"){
		const MemObj* mem = svf_pag->addDummyMemObj(ID, NULL);
		svf_pag->addFIObjNode(mem);
	}
	else if(node_type=="ValNode"){
		ValPN* node = new ValPN(ID,str_val,PAGNode::ValNode);
		svf_pag->addValNodeFromFile(str_val,node,ID);
	}
	else if(node_type == "DummyObjNode"){
		svf_pag->addDummyObjNode(ID,NULL);
	}
	else if(node_type=="ObjNode"){
		const MemObj* mem = svf_pag->addDummyMemObj(ID, NULL);
		ObjPN* node = new ObjPN(ID,str_val,mem,PAGNode::ObjNode);
		svf_pag->addObjNodeFromFile(str_val,node,ID);
	}
	else if(node_type == "RetNode"){
		RetPN *node = new RetPN(str_val,ID,PAGNode::RetNode);
		svf_pag->addRetNodeFromFile(str_val,node,ID);
	}
	else if(node_type == "VarargNode"){
		VarArgPN* node = new VarArgPN(ID,str_val,PAGNode::VarargNode);
		svf_pag->addVarargNodeFromFile(str_val,node,ID);
	}
	
}

//build pag from icfg file
PAG* PAGBuilderFromFile::build(){
	outs()<<"start building pag from icfg....\n";
	if(svf_pag == NULL){
		svf_pag = new SVFPAG(true);
	}
	ifstream myfile(file.c_str());
	if(myfile.is_open()){
		std::stringstream jsonStringStream;
		while(myfile >> jsonStringStream.rdbuf());
		llvm::json::Value root_value = llvm::json::parse(jsonStringStream.str()).get();
		llvm::json::Array* root_array = root_value.getAsArray();
		for(llvm::json::Array::const_iterator it = root_array->begin(); 
		it!=root_array->end();it++){
			llvm::json::Value ICFG_Node_obj_val = *it;
			llvm::json::Object* ICFG_Node_obj = ICFG_Node_obj_val.getAsObject();
			string node_type = ICFG_Node_obj->get("Node Type")->getAsString()->str();
			//add pag edges to IntraBlock node
			if(node_type == "IntraBlock"){
				llvm::json::Array* pag_edges_array = ICFG_Node_obj->get("PAG Edges")->getAsArray();
				for(llvm::json::Array::const_iterator eit = pag_edges_array->begin();
				eit!=pag_edges_array->end();eit++){
					llvm::json::Value edge_value = *eit;
					llvm::json::Object* edge_obj = edge_value.getAsObject();
					NodeID source_node = edge_obj->get("Source Node")->getAsInteger().getValue();
					NodeID destination_node = edge_obj->get("Destination Node")->getAsInteger().getValue();
					string source_node_type = edge_obj->get("Source Type")->getAsString()->str();
					string destination_node_type = edge_obj->get("Destination Type")->getAsString()->str();
					string edge_type = edge_obj->get("Edge Type")->getAsString()->str();
					llvm::json::Value* offset_value = edge_obj->get("offset");
					string offset;
					if(offset_value!=NULL){
						offset = offset_value->getAsString()->str();
					}

					//add new node
					string var = "";
					const std::string val = ""; 
					if(!svf_pag->hasGNode(source_node))
						addNode(source_node,source_node_type,val);
					if(!svf_pag->hasGNode(destination_node))
						addNode(destination_node,destination_node_type,val);
					if(svf_pag->hasGNode(source_node)&&svf_pag->hasGNode(destination_node)){
						if(offset!="")
							addEdge(source_node, destination_node, std::stol(offset), edge_type);
						else
							addEdge(source_node, destination_node, NULL, edge_type);
					}
				}
			}
		}
		myfile.close();
	}else{
		outs() << "Unable to open file\n";
	}

	/// new gep node's id from lower bound, nodeNum may not reflect the total nodes.
	u32_t lower_bound = gepNodeNumIndex;
	for(u32_t i = 0; i < lower_bound; i++)
		svf_pag->incNodeNum();

    svf_pag->setNodeNumAfterPAGBuild(svf_pag->getTotalNodeNum());

	return svf_pag;
}

/*!
 * Add PAG edge according to a file format
 */
void PAGBuilderFromFile::addEdge(NodeID srcID, NodeID dstID,
                                 Size_t offsetOrCSId, std::string edge) {

    //check whether these two nodes available
    PAGNode* srcNode = svf_pag->getPAGNode(srcID);
    PAGNode* dstNode = svf_pag->getPAGNode(dstID);

    if (edge == "Addr"){
        svf_pag->addAddrPE(srcID, dstID);
    }
    else if (edge == "Copy"){
		svf_pag->addCopyPE(srcID, dstID);
	}
    else if (edge == "Load"){
		svf_pag->addLoadPE(srcID, dstID);
	}
    else if (edge == "Store"){
		//svf_pag->addStorePE(srcID, dstID);
	}
    else if (edge == "NormalGep"){
		svf_pag->addNormalGepPE(srcID, dstID, LocationSet(offsetOrCSId));	
	}
    else if (edge == "VariantGep"){
		svf_pag->addVariantGepPE(srcID, dstID);
	}
    else if (edge == "Call"){
        svf_pag->addEdge(srcNode, dstNode, new CallPE(srcNode, dstNode, NULL));
	}
    else if (edge == "Ret"){
        svf_pag->addEdge(srcNode, dstNode, new RetPE(srcNode, dstNode, NULL));
	}
    else if (edge == "Cmp"){
        svf_pag->addCmpPE(srcID, dstID);
	}
    else if (edge == "BinaryOp"){
        svf_pag->addBinaryOPPE(srcID, dstID);
	}
    else
        assert(false && "format not support, can not create such edge");
}

ICFG* ICFGBuilderFromFile::build(){
    icfg = new ICFG();
    ifstream myfile(file.c_str());
	if(myfile.is_open()){
        //read the json stream from file
        std::stringstream jsonStringStream;
		while(myfile >> jsonStringStream.rdbuf());

        //parse the json data 
        llvm::json::Value root_value = llvm::json::parse(jsonStringStream.str()).get();
		llvm::json::Array* root_array = root_value.getAsArray();
       
       //add all node 
        for(llvm::json::Array::const_iterator it = root_array->begin(); 
		it!=root_array->end();it++){
            llvm::json::Value ICFG_Node_obj_val = *it;
			llvm::json::Object* ICFG_Node_obj = ICFG_Node_obj_val.getAsObject();
			string node_type = ICFG_Node_obj->get("Node Type")->getAsString()->str();
            NodeID nodeID = ICFG_Node_obj->get("ICFG_ID")->getAsInteger().getValue();
            if(node_type=="IntraBlock"){
                llvm::json::Array* pagEdges = ICFG_Node_obj->get("PAG Edges")->getAsArray();
                addNode(nodeID,node_type,pagEdges);
            }
            addNode(nodeID,node_type,NULL);
        }
        
        //add all edges
        for(llvm::json::Array::const_iterator it = root_array->begin(); 
		it!=root_array->end();it++){
            llvm::json::Value ICFG_Node_obj_val = *it;
			llvm::json::Object* ICFG_Node_obj = ICFG_Node_obj_val.getAsObject();
			llvm::json::Array* icfg_edges_array = ICFG_Node_obj->get("ICFGEdges")->getAsArray();
            for(llvm::json::Array::const_iterator eit = icfg_edges_array->begin();
            eit!=icfg_edges_array->end();eit++){
                llvm::json::Value icfgEdge_value = *eit;
				llvm::json::Object* icfg_edge_obj = icfgEdge_value.getAsObject();
                addEdge(icfg_edge_obj);
            }
        }
    }
	return icfg;
}

void ICFGBuilderFromFile::addNode(NodeID nodeId, std::string nodeType,llvm::json::Array* pagEdges){
    const std::string value = "";
    SVFFunction* svf_function = new SVFFunction("");
    if(!icfg->hasICFGNode(nodeId)){
        if(nodeType=="IntraBlock"){
            IntraBlockNode *node = new IntraBlockNode(nodeId,value);
            for(llvm::json::Array::const_iterator it = pagEdges->begin(); it!=pagEdges->end();it++){
                llvm::json::Value pagEdge_value = *it;
                llvm::json::Object* pagEdge = pagEdge_value.getAsObject();
                NodeID source_node = pagEdge->get("Source Node")->getAsInteger().getValue();
				NodeID destination_node = pagEdge->get("Destination Node")->getAsInteger().getValue();
				string source_node_type = pagEdge->get("Source Type")->getAsString()->str();
				string destination_node_type = pagEdge->get("Destination Type")->getAsString()->str();
				string edge_type = pagEdge->get("Edge Type")->getAsString()->str();
				llvm::json::Value* offset_value = pagEdge->get("offset");
				string offset;
				if(offset_value!=NULL){
					offset = offset_value->getAsString()->str();
				}
                PAGNode* srcNode = pag->getPAGNode(source_node);
                PAGNode* dstNode = pag->getPAGNode(destination_node);
                PAGEdge* edge = NULL;
                if (edge_type == "Addr"){
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::Addr);
                }
                else if (edge_type == "Copy")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::Copy);
                else if (edge_type == "Load")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::Load);
                else if (edge_type == "Store")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::Store);
                else if (edge_type == "NormalGep")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::NormalGep);
                else if (edge_type == "VariantGep")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::VariantGep);
                else if (edge_type == "Call")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::Call);
                else if (edge_type == "Ret")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::Ret);
                else if (edge_type == "Cmp")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::Cmp);
                else if (edge_type == "BinaryOp")
                    edge = pag->getIntraPAGEdge(srcNode,dstNode,PAGEdge::BinaryOp);
                else
                    assert(false && "format not support, can not create such edge");
                //add pag edge to the node
                if(edge!=NULL)
                    node->addPAGEdge(edge);
            }
            icfg->addGNode(nodeId,node);
            outs()<<"adding IntraBlock node....\n";
        }
        else if(nodeType=="FunEntryBlock"){
            FunEntryBlockNode *node = new FunEntryBlockNode(nodeId,svf_function);
            icfg->addGNode(nodeId,node);
            outs()<<"adding FunEntryBlock node....\n";
        }
        else if(nodeType=="FunExitBlock"){
            FunExitBlockNode *node = new FunExitBlockNode(nodeId,svf_function);
            icfg->addGNode(nodeId,node);
            outs()<<"adding FunExitBlock node....\n";
        }
        else if(nodeType=="FunCallBlock"){
            CallBlockNode *node = new CallBlockNode(nodeId,*(new CallSite()));
            icfg->addGNode(nodeId,node);
            outs()<<"adding FunCallBlock node....\n";
        }
        else if(nodeType=="FunRetBlock"){
            CallBlockNode *callnode = new CallBlockNode(nodeId,*(new CallSite()));
            RetBlockNode *node = new RetBlockNode(nodeId,*(new CallSite()),callnode);
            icfg->addGNode(nodeId,node);
            outs()<<"adding FunRetBlock node....\n";
        }
    }
}

void ICFGBuilderFromFile::addEdge(llvm::json::Object* edge_obj){
    NodeID srcNodeID = edge_obj->get("ICFGEdgeSrcID")->getAsInteger().getValue();
    NodeID dstNodeID = edge_obj->get("ICFGEdgeDstID")->getAsInteger().getValue();
    ICFGNode* srcICFGNode = icfg->getICFGNode(srcNodeID);
    ICFGNode* dstICFGNode = icfg->getICFGNode(dstNodeID);
    std::string icfg_edge_type = edge_obj->get("ICFG Edge Type")->getAsString()->str();
    if(icfg_edge_type=="IntraCFGEdge"){
        icfg->addIntraEdge(srcICFGNode,dstICFGNode);
    }
    else if(icfg_edge_type=="RetCFGEdge"){
        CallSiteID csID = edge_obj->get("csID")->getAsInteger().getValue();
        icfg->addRetEdge(srcICFGNode,dstICFGNode,*(new CallSite()));
    }
    else if(icfg_edge_type=="CallCFGEdge"){
        CallSiteID csID = edge_obj->get("csID")->getAsInteger().getValue();
        icfg->addCallEdge(srcICFGNode,dstICFGNode,*(new CallSite()));
    }
}

PTACallGraph* CallGraphBuilderFromFile::build(){
    outs()<<"Building call graph from file ....\n";
    ptaCallGraph = new PTACallGraph(PTACallGraph::NormCallGraph);
    ifstream myfile(file.c_str());
    if(myfile.is_open()){
        //read the json stream from file
        std::stringstream jsonStringStream;
        while (myfile>>jsonStringStream.rdbuf());

        //parse the json data 
        llvm::json::Value root_value = llvm::json::parse(jsonStringStream.str()).get();
		llvm::json::Array* root_array = root_value.getAsArray();

        //add all nodes
         for( llvm::json::Array::const_iterator it = root_array->begin(); 
		it != root_array->end(); it++ ){
            llvm::json::Value ICFG_Node_obj_val = *it;
			llvm::json::Object* ICFG_Node_obj = ICFG_Node_obj_val.getAsObject();
			string node_type = ICFG_Node_obj->get("Node Type")->getAsString()->str();
            NodeID nodeID = ICFG_Node_obj->get("ICFG_ID")->getAsInteger().getValue();
            const std::string *value = new std::string("");
            SVFFunction* svf_function = new SVFFunction("");
            CallBlockNode *call_node = new CallBlockNode(nodeID,*(new CallSite()));
            if(node_type=="FunEntryBlock"){
                const FunEntryBlockNode *node = new FunEntryBlockNode(nodeID,svf_function);
                ptaCallGraph->addCallGraphNode(svf_function);
                const SVFFunction* callee = new SVFFunction("");
                const SVFFunction* caller = new SVFFunction("");
                ptaCallGraph->addDirectCallGraphEdge(call_node,caller,callee);
            }
            else if(node_type=="FunExitBlock"){
                const FunExitBlockNode *node = new FunExitBlockNode(nodeID,svf_function);
                ptaCallGraph->addCallGraphNode(svf_function);
                const SVFFunction* callee = new SVFFunction("");
                const SVFFunction* caller = new SVFFunction("");
                ptaCallGraph->addDirectCallGraphEdge(call_node,caller,callee);
            }
            else if(node_type=="FunCallBlock"){
                CallBlockNode *node = new CallBlockNode(nodeID,*(new CallSite()));
                ptaCallGraph->addCallGraphNode(svf_function);
                const SVFFunction* callee = new SVFFunction("");
                const SVFFunction* caller = new SVFFunction("");
                ptaCallGraph->addDirectCallGraphEdge(call_node,caller,callee);
            }
            else if(node_type=="FunRetBlock"){
                CallBlockNode *call_node = new CallBlockNode(nodeID,*(new CallSite()));
                const RetBlockNode *node = new RetBlockNode(nodeID,*(new CallSite()),call_node);
                ptaCallGraph->addCallGraphNode(svf_function);
                const SVFFunction* callee = new SVFFunction("");
                const SVFFunction* caller = new SVFFunction("");
                ptaCallGraph->addDirectCallGraphEdge(call_node,caller,callee);
            }
        }
    }
    return ptaCallGraph;
}
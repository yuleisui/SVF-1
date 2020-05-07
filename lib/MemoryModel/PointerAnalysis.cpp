//===- PointerAnalysis.cpp -- Base class of pointer analyses------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2017>  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * PointerAnalysis.cpp
 *
 *  Created on: May 14, 2013
 *      Author: Yulei Sui
 */

#include "SVF-FE/PAGBuilder.h"
#include "SVF-FE/CallGraphBuilder.h"
#include "SVF-FE/CHA.h"
#include "SVF-FE/CPPUtil.h"
#include "Util/SVFModule.h"
#include "Util/SVFUtil.h"
#include "SVF-FE/LLVMUtil.h"

#include "MemoryModel/PointerAnalysisImpl.h"
#include "MemoryModel/Json2Graph.h"
#include "MemoryModel/PTAStat.h"
#include "Graphs/ThreadCallGraph.h"
#include "Graphs/ICFG.h"
#include "MemoryModel/PTAType.h"
#include "Graphs/ExternalPAG.h"
#include <fstream>
#include <sstream>

using namespace SVFUtil;
using namespace cppUtil;

static llvm::cl::opt<bool> TYPEPrint("print-type", llvm::cl::init(false),
                               llvm::cl::desc("Print type"));

static llvm::cl::opt<bool> FuncPointerPrint("print-fp", llvm::cl::init(false),
                                      llvm::cl::desc("Print targets of indirect call site"));

static llvm::cl::opt<bool> PTSPrint("print-pts", llvm::cl::init(false),
                              llvm::cl::desc("Print points-to set of top-level pointers"));

static llvm::cl::opt<bool> PTSAllPrint("print-all-pts", llvm::cl::init(false),
                                 llvm::cl::desc("Print all points-to set of both top-level and address-taken variables"));

static llvm::cl::opt<bool> PStat("stat", llvm::cl::init(true),
                           llvm::cl::desc("Statistic for Pointer analysis"));

static llvm::cl::opt<unsigned> statBudget("statlimit",  llvm::cl::init(20),
                                    llvm::cl::desc("Iteration budget for On-the-fly statistics"));

static llvm::cl::opt<bool> PAGDotGraph("dump-pag", llvm::cl::init(false),
                                 llvm::cl::desc("Dump dot graph of PAG"));

static llvm::cl::opt<bool> PAGPrint("print-pag", llvm::cl::init(false),
                              llvm::cl::desc("Print PAG to command line"));

static llvm::cl::opt<unsigned> IndirectCallLimit("indCallLimit",  llvm::cl::init(50000),
        llvm::cl::desc("Indirect solved call edge limit"));

static llvm::cl::opt<bool> UsePreCompFieldSensitive("preFieldSensitive", llvm::cl::init(true),
        llvm::cl::desc("Use pre-computed field-sensitivity for later analysis"));

static llvm::cl::opt<bool> EnableAliasCheck("alias-check", llvm::cl::init(true),
                                      llvm::cl::desc("Enable alias check functions"));

static llvm::cl::opt<bool> EnableThreadCallGraph("enable-tcg", llvm::cl::init(true),
        llvm::cl::desc("Enable pointer analysis to use thread call graph"));

static llvm::cl::opt<bool> connectVCallOnCHA("vcall-cha", llvm::cl::init(false),
                                       llvm::cl::desc("connect virtual calls using cha"));

CHGraph* PointerAnalysis::chgraph = NULL;
PAG* PointerAnalysis::pag = NULL;

/*!
 * Constructor
 */
PointerAnalysis::PointerAnalysis(PTATY ty, bool alias_check) :
    ptaTy(ty),stat(NULL),ptaCallGraph(NULL),callGraphSCC(NULL),typeSystem(NULL), icfg(NULL), svfMod(NULL) {
    OnTheFlyIterBudgetForStat = statBudget;
    print_stat = PStat;
    alias_validation = (alias_check && EnableAliasCheck);
}

/*!
 * Destructor
 */
PointerAnalysis::~PointerAnalysis() {
    destroy();
    // do not delete the PAG for now
    //delete pag;
}


void PointerAnalysis::destroy()
{
    delete ptaCallGraph;
    ptaCallGraph = NULL;

    delete callGraphSCC;
    callGraphSCC = NULL;

    delete stat;
    stat = NULL;

    delete typeSystem;
    typeSystem = NULL;
}

/*!
 * Initialization of pointer analysis
 */
void PointerAnalysis::initialize(SVFModule* svfModule) {

    /// whether we have already built PAG
    if(pag == NULL) {

        DBOUT(DGENERAL, outs() << pasMsg("Building PAG ...\n"));
        // We read PAG from a user-defined txt instead of parsing PAG from LLVM IR
        if (SVFModule::pagReadFromTXT()) {
            PAGBuilderFromFile fileBuilder(SVFModule::pagFileName());
            pag = fileBuilder.build();
            ICFGBuilderFromFile icfgFileBuilder(SVFModule::pagFileName(),pag);
            icfg = icfgFileBuilder.build();
            CallGraphBuilderFromFile callGraphBuilder (SVFModule::pagFileName());
            ptaCallGraph = callGraphBuilder.build();

        } else {
            DBOUT(DGENERAL, outs() << pasMsg("Building Symbol table ...\n"));
            SymbolTableInfo* symTable = SymbolTableInfo::Symbolnfo();
            symTable->buildMemModel(svfModule);

            PAGBuilder builder;
            pag = builder.build(svfModule);

            chgraph = new CHGraph(svfModule);
            chgraph->buildCHG();

            //typeSystem = new TypeSystem(pag);
        }

        // dump the PAG graph
        if (dumpGraph())
            PAG::getPAG()->dump("pag_initial");

        // print to command line of the PAG graph
        if (PAGPrint)
            pag->print();
    }

    /// initialise pta call graph for every pointer analysis instance
    if(EnableThreadCallGraph) {
    	ThreadCallGraph* cg = new ThreadCallGraph();
    	ThreadCallGraphBuilder bd(cg, pag->getICFG());
        ptaCallGraph = bd.buildThreadCallGraph(svfModule);
    }
    else {
    	PTACallGraph* cg = new PTACallGraph();
        CallGraphBuilder bd(cg,pag->getICFG());
        ptaCallGraph = bd.buildCallGraph(svfModule);
    }
    callGraphSCCDetection();
    svfMod = svfModule;
}


/*!
 * Return TRUE if this node is a local variable of recursive function.
 */
bool PointerAnalysis::isLocalVarInRecursiveFun(NodeID id) const
{
    const MemObj* obj = this->pag->getObject(id);
    assert(obj && "object not found!!");
    if(obj->isStack()) {
        if(const AllocaInst* local = SVFUtil::dyn_cast<AllocaInst>(obj->getRefVal())) {
            const SVFFunction* fun = LLVMModuleSet::getLLVMModuleSet()->getSVFFunction(local->getFunction());
            return callGraphSCC->isInCycle(getPTACallGraph()->getCallGraphNode(fun)->getId());
        }
    }
    return false;
}

/*!
 * Reset field sensitivity
 */
void PointerAnalysis::resetObjFieldSensitive()
{
    for (PAG::iterator nIter = pag->begin(); nIter != pag->end(); ++nIter) {
        if(ObjPN* node = SVFUtil::dyn_cast<ObjPN>(nIter->second))
            const_cast<MemObj*>(node->getMemObj())->setFieldSensitive();
    }
}

/*!
 * Flag in order to dump graph
 */
bool PointerAnalysis::dumpGraph() {
    return PAGDotGraph;
}

/*
 * Dump statistics
 */

void PointerAnalysis::dumpStat() {

    if(print_stat && stat)
        stat->performStat();
}


/*!
 * Finalize the analysis after solving
 * Given the alias results, verify whether it is correct or not using alias check functions
 */
void PointerAnalysis::finalize() {

    /// Print statistics
    dumpStat();

    PAG* pag = PAG::getPAG();
    // dump the PAG graph
    if (dumpGraph())
        pag->dump("pag_final");

    if (!DumpPAGFunctions.empty()) ExternalPAG::dumpFunctions(DumpPAGFunctions);

    /// Dump results
    if (PTSPrint) {
        dumpTopLevelPtsTo();
        //dumpAllPts();
        //dumpCPts();
    }

    if (TYPEPrint)
        dumpAllTypes();

    if(PTSAllPrint)
        dumpAllPts();

    if (FuncPointerPrint)
        printIndCSTargets();

    getPTACallGraph()->verifyCallGraph();

    getPTACallGraph()->dump("callgraph_final");

    if(!pag->isBuiltFromFile() && alias_validation)
        validateTests();

    if (!UsePreCompFieldSensitive)
        resetObjFieldSensitive();
}

/*!
 * Validate test cases
 */
void PointerAnalysis::validateTests() {
    validateSuccessTests("MAYALIAS");
    validateSuccessTests("NOALIAS");
    validateSuccessTests("MUSTALIAS");
    validateSuccessTests("PARTIALALIAS");
    validateSuccessTests("_Z8MAYALIASPvS_");
    validateSuccessTests("_Z7NOALIASPvS_");
    validateSuccessTests("_Z9MUSTALIASPvS_");
    validateSuccessTests("_Z12PARTIALALIASPvS_");
    validateExpectedFailureTests("EXPECTEDFAIL_MAYALIAS");
    validateExpectedFailureTests("EXPECTEDFAIL_NOALIAS");
}


void PointerAnalysis::dumpAllTypes()
{
    for (NodeSet::iterator nIter = this->getAllValidPtrs().begin();
            nIter != this->getAllValidPtrs().end(); ++nIter) {
        const PAGNode* node = getPAG()->getPAGNode(*nIter);
        if (SVFUtil::isa<DummyObjPN>(node) || SVFUtil::isa<DummyValPN>(node))
            continue;

        outs() << "##<" << node->getValue()->getName() << "> ";
        outs() << "Source Loc: " << getSourceLoc(node->getValue());
        outs() << "\nNodeID " << node->getId() << "\n";

        Type* type = node->getValue()->getType();
        SymbolTableInfo::Symbolnfo()->printFlattenFields(type);
        if (PointerType* ptType = SVFUtil::dyn_cast<PointerType>(type))
            SymbolTableInfo::Symbolnfo()->printFlattenFields(ptType->getElementType());
    }
}

/*!
 * Dump points-to of top-level pointers (ValPN)
 */
void PointerAnalysis::dumpPts(NodeID ptr, const PointsTo& pts) {

    const PAGNode* node = pag->getPAGNode(ptr);
    /// print the points-to set of node which has the maximum pts size.
    if (SVFUtil::isa<DummyObjPN> (node)) {
        outs() << "##<Dummy Obj > id:" << node->getId();
    } else if (!SVFUtil::isa<DummyValPN>(node) && !SVFModule::pagReadFromTXT()) {
        outs() << "##<" << node->getValue()->getName() << "> ";
        outs() << "Source Loc: " << getSourceLoc(node->getValue());
    }
    outs() << "\nPtr " << node->getId() << " ";

    if (pts.empty()) {
        outs() << "\t\tPointsTo: {empty}\n\n";
    } else {
        outs() << "\t\tPointsTo: { ";
        for (PointsTo::iterator it = pts.begin(), eit = pts.end(); it != eit;
                ++it)
            outs() << *it << " ";
        outs() << "}\n\n";
    }

    outs() << "";

    for (NodeBS::iterator it = pts.begin(), eit = pts.end(); it != eit; ++it) {
        const PAGNode* node = pag->getPAGNode(*it);
        if(SVFUtil::isa<ObjPN>(node) == false)
            continue;
        NodeID ptd = node->getId();
        outs() << "!!Target NodeID " << ptd << "\t [";
        const PAGNode* pagNode = pag->getPAGNode(ptd);
        if (SVFUtil::isa<DummyValPN>(node))
            outs() << "DummyVal\n";
        else if (SVFUtil::isa<DummyObjPN>(node))
            outs() << "Dummy Obj id: " << node->getId() << "]\n";
        else {
        		if(!SVFModule::pagReadFromTXT()){
        			outs() << "<" << pagNode->getValue()->getName() << "> ";
        			outs() << "Source Loc: " << getSourceLoc(pagNode->getValue()) << "] \n";
        		}
        }
    }
}

/*!
 * Print indirect call targets at an indirect callsite
 */
void PointerAnalysis::printIndCSTargets(const CallBlockNode* cs, const FunctionSet& targets)
{
    outs() << "\nNodeID: " << getFunPtr(cs);
    outs() << "\nCallSite: ";
    cs->getCallSite().getInstruction()->print(outs());
    outs() << "\tLocation: " << SVFUtil::getSourceLoc(cs->getCallSite().getInstruction());
    outs() << "\t with Targets: ";

    if (!targets.empty()) {
        FunctionSet::const_iterator fit = targets.begin();
        FunctionSet::const_iterator feit = targets.end();
        for (; fit != feit; ++fit) {
            const SVFFunction* callee = *fit;
            outs() << "\n\t" << callee->getName();
        }
    }
    else {
        outs() << "\n\tNo Targets!";
    }

    outs() << "\n";
}

/*!
 * Print all indirect callsites
 */
void PointerAnalysis::printIndCSTargets()
{
    outs() << "==================Function Pointer Targets==================\n";
    const CallEdgeMap& callEdges = getIndCallMap();
    CallEdgeMap::const_iterator it = callEdges.begin();
    CallEdgeMap::const_iterator eit = callEdges.end();
    for (; it != eit; ++it) {
        const CallBlockNode* cs = it->first;
        const FunctionSet& targets = it->second;
        printIndCSTargets(cs, targets);
    }

    const CallSiteToFunPtrMap& indCS = getIndirectCallsites();
    CallSiteToFunPtrMap::const_iterator csIt = indCS.begin();
    CallSiteToFunPtrMap::const_iterator csEit = indCS.end();
    for (; csIt != csEit; ++csIt) {
        const CallBlockNode* cs = csIt->first;
        if (hasIndCSCallees(cs) == false) {
            outs() << "\nNodeID: " << csIt->second;
            outs() << "\nCallSite: ";
            cs->getCallSite().getInstruction()->print(outs());
            outs() << "\tLocation: " << SVFUtil::getSourceLoc(cs->getCallSite().getInstruction());
            outs() << "\n\t!!!has no targets!!!\n";
        }
    }
}



/*!
 * Resolve indirect calls
 */
void PointerAnalysis::resolveIndCalls(const CallBlockNode* cs, const PointsTo& target, CallEdgeMap& newEdges,LLVMCallGraph* callgraph) {

    assert(pag->isIndirectCallSites(cs) && "not an indirect callsite?");
    /// discover indirect pointer target
    for (PointsTo::iterator ii = target.begin(), ie = target.end();
            ii != ie; ii++) {

        if(getNumOfResolvedIndCallEdge() >= IndirectCallLimit) {
            errMsg("Resolved Indirect Call Edges are Out-Of-Budget, please increase the limit");
            return;
        }

        if(ObjPN* objPN = SVFUtil::dyn_cast<ObjPN>(pag->getPAGNode(*ii))) {
            const MemObj* obj = pag->getObject(objPN);

            if(obj->isFunction()) {
                const Function* calleefun = SVFUtil::cast<Function>(obj->getRefVal());
                const SVFFunction* callee = getDefFunForMultipleModule(calleefun);

                /// if the arg size does not match then we do not need to connect this parameter
                /// even if the callee is a variadic function (the first parameter of variadic function is its paramter number)
                if(matchArgs(cs, callee) == false)
                    continue;

                if(0 == getIndCallMap()[cs].count(callee)) {
                    newEdges[cs].insert(callee);
                    getIndCallMap()[cs].insert(callee);

                    ptaCallGraph->addIndirectCallGraphEdge(cs, cs->getCaller(), callee);
                    // FIXME: do we need to update llvm call graph here?
                    // The indirect call is maintained by ourself, We may update llvm's when we need to
                    //CallGraphNode* callgraphNode = callgraph->getOrInsertFunction(cs.getCaller());
                    //callgraphNode->addCalledFunction(cs,callgraph->getOrInsertFunction(callee));
                }
            }
        }
    }
}

/*!
 * Match arguments for callsite at caller and callee
 */
bool PointerAnalysis::matchArgs(const CallBlockNode* cs, const SVFFunction* callee) {
    if(ThreadAPI::getThreadAPI()->isTDFork(cs->getCallSite()))
        return true;
    else
        return cs->getCallSite().arg_size() == callee->arg_size();
}

/*
 * Get virtual functions "vfns" based on CHA
 */
void PointerAnalysis::getVFnsFromCHA(const CallBlockNode* cs, VFunSet &vfns) {
    if (chgraph->csHasVFnsBasedonCHA(cs->getCallSite()))
        vfns = chgraph->getCSVFsBasedonCHA(cs->getCallSite());
}

/*
 * Get virtual functions "vfns" from PoninsTo set "target" for callsite "cs"
 */
void PointerAnalysis::getVFnsFromPts(const CallBlockNode* cs, const PointsTo &target, VFunSet &vfns) {

    if (chgraph->csHasVtblsBasedonCHA(cs->getCallSite())) {
        std::set<const GlobalValue*> vtbls;
        const VTableSet &chaVtbls = chgraph->getCSVtblsBasedonCHA(cs->getCallSite());
        for (PointsTo::iterator it = target.begin(), eit = target.end(); it != eit; ++it) {
            const PAGNode *ptdnode = pag->getPAGNode(*it);
			if (ptdnode->hasValue()) {
				if (const GlobalValue *vtbl = SVFUtil::dyn_cast<GlobalValue>(ptdnode->getValue())) {
					if (chaVtbls.find(vtbl) != chaVtbls.end())
						vtbls.insert(vtbl);
				}
			}
        }
        chgraph->getVFnsFromVtbls(cs->getCallSite(), vtbls, vfns);
    }
}

/*
 * Connect callsite "cs" to virtual functions in "vfns"
 */
void PointerAnalysis::connectVCallToVFns(const CallBlockNode* cs, const VFunSet &vfns, CallEdgeMap& newEdges) {
    //// connect all valid functions
    for (VFunSet::const_iterator fit = vfns.begin(),
            feit = vfns.end(); fit != feit; ++fit) {
        const SVFFunction* callee = *fit;
        callee = getDefFunForMultipleModule(callee->getLLVMFun());
        if (getIndCallMap()[cs].count(callee) > 0)
            continue;
        if(cs->getCallSite().arg_size() == callee->arg_size() ||
                (cs->getCallSite().getFunctionType()->isVarArg() && callee->isVarArg())) {
            newEdges[cs].insert(callee);
            getIndCallMap()[cs].insert(callee);
            const CallBlockNode* callBlockNode = pag->getICFG()->getCallBlockNode(cs->getCallSite().getInstruction());
            ptaCallGraph->addIndirectCallGraphEdge(callBlockNode, cs->getCaller(),callee);
        }
    }
}

/// Resolve cpp indirect call edges
void PointerAnalysis::resolveCPPIndCalls(const CallBlockNode* cs, const PointsTo& target, CallEdgeMap& newEdges) {
    assert(isVirtualCallSite(cs->getCallSite()) && "not cpp virtual call");

    VFunSet vfns;
    if (connectVCallOnCHA)
        getVFnsFromCHA(cs, vfns);
    else
        getVFnsFromPts(cs, target, vfns);
    connectVCallToVFns(cs, vfns, newEdges);
}

/*!
 * Find the alias check functions annotated in the C files
 * check whether the alias analysis results consistent with the alias check function itself
 */
void PointerAnalysis::validateSuccessTests(const char* fun) {

    // check for must alias cases, whether our alias analysis produce the correct results
        if (const SVFFunction* checkFun = getFunction(fun)) {
            if(!checkFun->getLLVMFun()->use_empty())
                outs() << "[" << this->PTAName() << "] Checking " << fun << "\n";

            for (Value::user_iterator i = checkFun->getLLVMFun()->user_begin(), e =
                        checkFun->getLLVMFun()->user_end(); i != e; ++i)
                if (SVFUtil::isa<CallInst>(*i) || SVFUtil::isa<InvokeInst>(*i)) {

                    CallSite cs(*i);
                    assert(cs.getNumArgOperands() == 2
                           && "arguments should be two pointers!!");
                    Value* V1 = cs.getArgOperand(0);
                    Value* V2 = cs.getArgOperand(1);
                    AliasResult aliasRes = alias(V1, V2);

                    bool checkSuccessful = false;
                    if (strcmp(fun, "MAYALIAS") == 0 || strcmp(fun, "_Z8MAYALIASPvS_") == 0) {
                        if (aliasRes == llvm::MayAlias || aliasRes == llvm::MustAlias)
                            checkSuccessful = true;
                    } else if (strcmp(fun, "NOALIAS") == 0 || strcmp(fun, "_Z7NOALIASPvS_") == 0) {
                        if (aliasRes == llvm::NoAlias)
                            checkSuccessful = true;
                    } else if (strcmp(fun, "MUSTALIAS") == 0 || strcmp(fun, "_Z9MUSTALIASPvS_") == 0) {
                        // change to must alias when our analysis support it
                        if (aliasRes == llvm::MayAlias || aliasRes == llvm::MustAlias)
                            checkSuccessful = true;
                    } else if (strcmp(fun, "PARTIALALIAS") == 0 || strcmp(fun, "_Z12PARTIALALIASPvS_") == 0) {
                        // change to partial alias when our analysis support it
                        if (aliasRes == llvm::MayAlias)
                            checkSuccessful = true;
                    } else
                        assert(false && "not supported alias check!!");

                    NodeID id1 = pag->getValueNode(V1);
                    NodeID id2 = pag->getValueNode(V2);

                    if (checkSuccessful)
                        outs() << sucMsg("\t SUCCESS :") << fun << " check <id:" << id1 << ", id:" << id2 << "> at ("
                               << getSourceLoc(*i) << ")\n";
                    else
                        SVFUtil::errs() << errMsg("\t FAIL :") << fun << " check <id:" << id1 << ", id:" << id2 << "> at ("
                               << getSourceLoc(*i) << ")\n";
                } else
                    assert(false && "alias check functions not only used at callsite??");

        }
}

/*!
 * Pointer analysis validator
 */
void PointerAnalysis::validateExpectedFailureTests(const char* fun) {

    if (const SVFFunction* checkFun = getFunction(fun)) {
        if(!checkFun->getLLVMFun()->use_empty())
            outs() << "[" << this->PTAName() << "] Checking " << fun << "\n";

        for (Value::user_iterator i = checkFun->getLLVMFun()->user_begin(), e =
                    checkFun->getLLVMFun()->user_end(); i != e; ++i)
            if (CallInst *call = SVFUtil::dyn_cast<CallInst>(*i)) {
                assert(call->getNumArgOperands() == 2
                       && "arguments should be two pointers!!");
                Value* V1 = call->getArgOperand(0);
                Value* V2 = call->getArgOperand(1);
                AliasResult aliasRes = alias(V1, V2);

                bool expectedFailure = false;
                if (strcmp(fun, "EXPECTEDFAIL_MAYALIAS") == 0) {
                    // change to must alias when our analysis support it
                    if (aliasRes == llvm::NoAlias)
                        expectedFailure = true;
                } else if (strcmp(fun, "EXPECTEDFAIL_NOALIAS") == 0) {
                    // change to partial alias when our analysis support it
                    if (aliasRes == llvm::MayAlias || aliasRes == llvm::PartialAlias || aliasRes == llvm::MustAlias)
                        expectedFailure = true;
                } else
                    assert(false && "not supported alias check!!");

                NodeID id1 = pag->getValueNode(V1);
                NodeID id2 = pag->getValueNode(V2);

                if (expectedFailure)
                    outs() << sucMsg("\t EXPECTED FAIL :") << fun << " check <id:" << id1 << ", id:" << id2 << "> at ("
                           << getSourceLoc(call) << ")\n";
                else
                    SVFUtil::errs() << errMsg("\t UNEXPECTED FAIL :") << fun << " check <id:" << id1 << ", id:" << id2 << "> at ("
                           << getSourceLoc(call) << ")\n";
            }
            else
                assert(false && "alias check functions not only used at callsite??");
    }
}

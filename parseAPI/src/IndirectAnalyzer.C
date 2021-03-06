#include "dyntypes.h"
#include "IndirectAnalyzer.h"
#include "BoundFactCalculator.h"
#include "JumpTableFormatPred.h"
#include "JumpTableIndexPred.h"
#include "SymbolicExpression.h"
#include "IndirectASTVisitor.h"
#include "IA_IAPI.h"
#include "debug_parse.h"

#include "CodeObject.h"
#include "Graph.h"

#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Register.h"
#include "SymEval.h"
using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;

static bool IsIndexing(AST::Ptr node, AbsRegion &index) {
    RoseAST::Ptr n = boost::static_pointer_cast<RoseAST>(node);
    if (n->val().op != ROSEOperation::sMultOp &&
        n->val().op != ROSEOperation::uMultOp &&
	n->val().op != ROSEOperation::shiftLOp &&
	n->val().op != ROSEOperation::rotateLOp) return false;
    if (n->child(0)->getID() != AST::V_VariableAST) return false;
    if (n->child(1)->getID() != AST::V_ConstantAST) return false;
    VariableAST::Ptr var = boost::static_pointer_cast<VariableAST>(n->child(0));
    index = var->val().reg;
    return true;
}

static bool IsVariableArgumentFormat(AST::Ptr t, AbsRegion &index) {
    if (t->getID() != AST::V_RoseAST) {
        return false;
    }
    RoseAST::Ptr rt = boost::static_pointer_cast<RoseAST>(t);
    if (rt->val().op != ROSEOperation::addOp) {
        return false;
    }
    if (rt->child(0)->getID() != AST::V_ConstantAST || rt->child(1)->getID() != AST::V_RoseAST) {
        return false;
    }
    RoseAST::Ptr c1 = boost::static_pointer_cast<RoseAST>(rt->child(1));
    if (c1->val().op == ROSEOperation::addOp) {
        if (c1->child(0)->getID() == AST::V_RoseAST && c1->child(1)->getID() == AST::V_ConstantAST) {
	    RoseAST::Ptr lc = boost::static_pointer_cast<RoseAST>(c1->child(0));
	    ConstantAST::Ptr rc = boost::static_pointer_cast<ConstantAST>(c1->child(1));
	    if (lc->val().op == ROSEOperation::invertOp && rc->val().val == 1) {
	        return IsIndexing(lc->child(0), index);
	    }
	}
	return false;
    }
    return IsIndexing(rt->child(1), index);

}

bool IndirectControlFlowAnalyzer::NewJumpTableAnalysis(std::vector<std::pair< Address, Dyninst::ParseAPI::EdgeTypeEnum > >& outEdges) {
    parsing_printf("Apply indirect control flow analysis at %lx\n", block->last());
    parsing_printf("Looking for thunk\n");
boost::make_lock_guard(*func);
//  Find all blocks that reach the block containing the indirect jump
//  This is a prerequisit for finding thunks
    GetAllReachableBlock();
//  Now we try to find all thunks in this function.
//  We pass in the slice because we may need to add new ndoes.
    FindAllThunks();
//  Calculates all blocks that can reach
//  and be reachable from thunk blocks
    ReachFact rf(thunks);

    // Now we start with the indirect jump instruction,
    // to determine the format of the (potential) jump table
    const unsigned char * buf = (const unsigned char*) block->region()->getPtrToInstruction(block->last());
    InstructionDecoder dec(buf, InstructionDecoder::maxInstructionLength, block->obj()->cs()->getArch());
    Instruction insn = dec.decode();
    AssignmentConverter ac(true, false);
    vector<Assignment::Ptr> assignments;
    ac.convert(insn, block->last(), func, block, assignments);
    Slicer formatSlicer(assignments[0], block, func, false, false);

    SymbolicExpression se;
    se.cs = block->obj()->cs();
    se.cr = block->region();
    JumpTableFormatPred jtfp(func, block, rf, thunks, se);
    GraphPtr slice = formatSlicer.backwardSlice(jtfp);
    //parsing_printf("\tJump table format: %s\n", jtfp.format().c_str());
    // If the jump target expression is not in a form we recognize,
    // we do not try to resolve it
    parsing_printf("In function %s, Address %lx, jump target format %s, index loc %s, index variable %s, memory read loc %s, isJumpTableFormat %d\n", 
            func->name().c_str(), 
            block->last(), 
            jtfp.format().c_str(), 
            jtfp.indexLoc ? jtfp.indexLoc->format().c_str() : "null" , 
            jtfp.index.format().c_str(),
            jtfp.memLoc ? jtfp.memLoc->format().c_str() : "null",
	    jtfp.isJumpTableFormat());

    bool variableArguFormat = false;
    if (!jtfp.isJumpTableFormat()) {
	if (jtfp.jumpTargetExpr && func->entry() == block && IsVariableArgumentFormat(jtfp.jumpTargetExpr, jtfp.index)) {
	    parsing_printf("\tVariable number of arguments format, index %s\n", jtfp.index.format().c_str());
	    variableArguFormat = true;
	} else {
            return false;
	}
    }

    StridedInterval b;
    bool scanTable = false;
    if (!variableArguFormat) {
        Slicer indexSlicer(jtfp.indexLoc, jtfp.indexLoc->block(), func, false, false);
	JumpTableIndexPred jtip(func, block, jtfp.index, se);
	jtip.setSearchForControlFlowDep(true);
	slice = indexSlicer.backwardSlice(jtip);

        if (!jtip.findBound && block->obj()->cs()->getArch() != Arch_aarch64) {

            // After the slicing is done, we do one last check to
            // see if we can resolve the indirect jump by assuming
            // one byte read is in bound [0,255]
            GraphPtr g = jtip.BuildAnalysisGraph(indexSlicer.visitedEdges);
	    BoundFactsCalculator bfc(func, g, func->entry() == block,  true, se);
	    bfc.CalculateBoundedFacts();
	
	    StridedInterval target;
	    jtip.IsIndexBounded(g, bfc, target);
        }
        if (jtip.findBound) {
            parsing_printf(" find bound %s for %lx\n", jtip.bound.format().c_str(), block->last());
	    b = jtip.bound;
        } else {
            parsing_printf(" Cannot find bound, assume there are at most 256 entries and scan the table for indirect jump at %lx\n", block->last());
	    b = StridedInterval(1, 0, 255);
	    scanTable = true;
        }
    } else {
        b = StridedInterval(1, 0, 8);
    }
    std::vector<std::pair< Address, Dyninst::ParseAPI::EdgeTypeEnum > > jumpTableOutEdges;
    ReadTable(jtfp.jumpTargetExpr,
              jtfp.index,
	      b,
	      GetMemoryReadSize(jtfp.memLoc),
	      IsZeroExtend(jtfp.memLoc),
	      scanTable,
	      jtfp.constAddr,
	      jumpTableOutEdges);
    parsing_printf(", find %d edges\n", jumpTableOutEdges.size());
    outEdges.insert(outEdges.end(), jumpTableOutEdges.begin(), jumpTableOutEdges.end());
    return !jumpTableOutEdges.empty();
}						       




// Find all blocks that reach the block containing the indirect jump
void IndirectControlFlowAnalyzer::GetAllReachableBlock() {
    reachable.clear();
    queue<Block*> q;
    q.push(block);
    while (!q.empty()) {
        ParseAPI::Block *cur = q.front();
	q.pop();
	if (reachable.find(cur) != reachable.end()) continue;
	reachable.insert(cur);
    boost::lock_guard<Block> g(*cur);
	for (auto eit = cur->sources().begin(); eit != cur->sources().end(); ++eit)
	    if ((*eit)->intraproc()) {
	        if ((*eit)->src() == NULL) {
		    fprintf(stderr, "edge type = %d, target block [%lx, %lx)\n", (*eit)->type(), cur->start(), cur->end());
		}
	        q.push((*eit)->src());
	    }
    }

}


static Address ThunkAdjustment(Address afterThunk, MachRegister reg, ParseAPI::Block *b) {
    // After the call to thunk, there is usually
    // an add insturction like ADD ebx, OFFSET to adjust
    // the value coming out of thunk.
   
    const unsigned char* buf = (const unsigned char*) (b->region()->getPtrToInstruction(afterThunk));
    InstructionDecoder dec(buf, b->end() - b->start(), b->obj()->cs()->getArch());
    Instruction nextInsn = dec.decode();
    // It has to be an add
    if (nextInsn.getOperation().getID() != e_add) return 0;
    vector<Operand> operands;
    nextInsn.getOperands(operands);
    RegisterAST::Ptr regAST = boost::dynamic_pointer_cast<RegisterAST>(operands[0].getValue());
    // The first operand should be a register
    if (regAST == 0) return 0;
    if (regAST->getID() != reg) return 0;
    Result res = operands[1].getValue()->eval();
    // A not defined result means that
    // the second operand is not an immediate
    if (!res.defined) return 0;
    return res.convert<Address>();
}

void IndirectControlFlowAnalyzer::FindAllThunks() {
    // Enumuerate every block to find thunk
    for (auto bit = reachable.begin(); bit != reachable.end(); ++bit) {
        // We intentional treat a getting PC call as a special case that does not
	// end a basic block. So, we need to check every instruction to find all thunks
        ParseAPI::Block *b = *bit;
	const unsigned char* buf =
            (const unsigned char*)(b->region()->getPtrToInstruction(b->start()));
	if( buf == NULL ) {
	    parsing_printf("%s[%d]: failed to get pointer to instruction by offset\n",FILE__, __LINE__);
	    return;
	}
	InstructionDecoder dec(buf, b->end() - b->start(), b->obj()->cs()->getArch());
	InsnAdapter::IA_IAPI* block = InsnAdapter::IA_IAPI::makePlatformIA_IAPI(b->obj()->cs()->getArch(), dec, b->start(), b->obj() , b->region(), b->obj()->cs(), b);
	Address cur = b->start();
	while (cur < b->end()) {
	    if (block->getInstruction().getCategory() == c_CallInsn && block->isThunk()) {
	        bool valid;
		Address addr;
		boost::tie(valid, addr) = block->getCFT();
		const unsigned char *target = (const unsigned char *) b->region()->getPtrToInstruction(addr);
		InstructionDecoder targetChecker(target, InstructionDecoder::maxInstructionLength, b->obj()->cs()->getArch());
		Instruction thunkFirst = targetChecker.decode();
		set<RegisterAST::Ptr> thunkTargetRegs;
		thunkFirst.getWriteSet(thunkTargetRegs);
		
		for (auto curReg = thunkTargetRegs.begin(); curReg != thunkTargetRegs.end(); ++curReg) {
		    ThunkInfo t;
		    t.reg = (*curReg)->getID();
		    t.value = block->getAddr() + block->getInstruction().size();
		    t.value += ThunkAdjustment(t.value, t.reg, b);
		    t.block = b;
		    thunks.insert(make_pair(block->getAddr(), t));
		    parsing_printf("\tfind thunk at %lx, storing value %lx to %s\n", block->getAddr(), t.value , t.reg.name().c_str());
		}
	    }
	    cur += block->getInstruction().size();
	    if (cur < b->end()) block->advance();
	}
	delete block;
    }
}

void IndirectControlFlowAnalyzer::ReadTable(AST::Ptr jumpTargetExpr,
                                            AbsRegion index,
					    StridedInterval &indexBound,
					    int memoryReadSize,
					    bool isZeroExtend,
					    bool scanTable,
					    set<Address> &constAddr,
					    std::vector<std::pair<Address, Dyninst::ParseAPI::EdgeTypeEnum> > &targetEdges) {
    CodeSource *cs = block->obj()->cs();
    set<Address> jumpTargets;
    int start = 0;
    if (indexBound.low > 0) start = indexBound.low = start;
    for (int v = start; v <= indexBound.high; v += indexBound.stride) {
        JumpTableReadVisitor jtrv(index, v, cs, block->region(), isZeroExtend, memoryReadSize);
	jumpTargetExpr->accept(&jtrv);
	if (jtrv.valid && cs->isCode(jtrv.targetAddress)) {
	    bool stop = false;
	    // Assume that indirect jump should not jump beyond the function range.
	    // This assumption is shaky in terms of non-contiguous functions.
	    // But non-contiguous blocks tend not be reach by indirect jumps
	    if (func->src() == HINT) {
	        Hint h(block->last(), 0 , NULL, "");
		auto iter = upper_bound(cs->hints().begin(), cs->hints().end(), h);
		if (iter != cs->hints().begin()) {
		    iter--;
		    Address startAddr = iter->_addr;
		    int size = iter->_size;
		    if (jtrv.targetAddress < startAddr || jtrv.targetAddress >= startAddr + size) {
		        stop = true;
			parsing_printf("WARNING: resolving jump tables leads to address %lx, which is not in the function range specified in the symbol table\n", jtrv.targetAddress);
			parsing_printf("\tSymbol at %lx, end at %lx\n", startAddr, startAddr + size);
		    }
		}
	    }
	    if (scanTable && jumpTargets.size() > 0 && cs->getArch() != Arch_ppc64) {
	        // This is a tentative scan of the table, where we do not know the index size.
		// Typically, the table is layout in a way that the target address is increasing
		// or decreasing.
		auto smallIt = jumpTargets.begin();
		auto largeIt = jumpTargets.end();
		--largeIt;
		if (jtrv.targetAddress < *largeIt && jtrv.targetAddress > *smallIt) {
		    stop = true;
		    parsing_printf("WARNING: we do not know the index size and we are scanning the table. We stop now because the jump target seems to not increasing or decreasing\n");
		}
	    }
	    if (stop) break;
	    jumpTargets.insert(jtrv.targetAddress);
	} else {
	    // We have a bad entry. We stop here, as we have wrong information
	    // In this case, we keep the good entries
	    parsing_printf("WARNING: resolving jump tables leads to a bad address %lx\n", jtrv.targetAddress);
	    break;
	}
	if (indexBound.stride == 0) break;
    }
    for (auto ait = constAddr.begin(); ait != constAddr.end(); ++ait) {
        if (block->region()->isCode(*ait)) {
	    jumpTargets.insert(*ait);
	}
    }
    for (auto tit = jumpTargets.begin(); tit != jumpTargets.end(); ++tit) {
        targetEdges.push_back(make_pair(*tit, INDIRECT));
    }
}

int IndirectControlFlowAnalyzer::GetMemoryReadSize(Assignment::Ptr memLoc) {
    if (!memLoc) {
        parsing_printf("\tmemLoc is null\n");
        return 0;
    }
    Instruction i = memLoc->insn();
    std::vector<Operand> ops;
    i.getOperands(ops);
    parsing_printf("\t there are %d operands\n", ops.size());
    for (auto oit = ops.begin(); oit != ops.end(); ++oit) {
        Operand o = *oit;
	if (o.readsMemory()) {
	    Expression::Ptr exp = o.getValue();
	    return exp->size();
	}
    }
    return 0;
}

bool IndirectControlFlowAnalyzer::IsZeroExtend(Assignment::Ptr memLoc) {
    if (!memLoc) {
        parsing_printf("\tmemLoc is null\n");
        return false;
    }
    Instruction i = memLoc->insn();
    parsing_printf("check zero extend %s\n", i.format().c_str());
    if (i.format().find("movz") != string::npos) return true;
    return false;
}

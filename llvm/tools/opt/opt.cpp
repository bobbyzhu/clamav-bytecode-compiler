//===- opt.cpp - The LLVM Modular Optimizer -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Optimizations may be specified an arbitrary number of times on the command
// line, They are run in the order specified.
//
//===----------------------------------------------------------------------===//

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/CallGraphSCCPass.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/PassNameParser.h"
#include "llvm/System/Signals.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/IRReader.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/StandardPasses.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/LinkAllVMCore.h"
#include <memory>
#include <algorithm>
using namespace llvm;

// The OptimizationList is automatically populated with registered Passes by the
// PassNameParser.
//
static cl::list<const PassInfo*, bool, PassNameParser>
PassList(cl::desc("Optimizations available:"));

// Other command line options...
//
static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"),
               cl::value_desc("filename"), cl::init("-"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
PrintEachXForm("p", cl::desc("Print module after each transformation"));

static cl::opt<bool>
NoOutput("disable-output",
         cl::desc("Do not write result bitcode file"), cl::Hidden);

static cl::opt<bool>
OutputAssembly("S", cl::desc("Write output as LLVM assembly"));

static cl::opt<bool>
NoVerify("disable-verify", cl::desc("Do not verify result module"), cl::Hidden);

static cl::opt<bool>
VerifyEach("verify-each", cl::desc("Verify after each transform"));

static cl::opt<bool>
StripDebug("strip-debug",
           cl::desc("Strip debugger symbol info from translation unit"));

static cl::opt<bool>
DisableInline("disable-inlining", cl::desc("Do not run the inliner pass"));

static cl::opt<bool>
DisableOptimizations("disable-opt",
                     cl::desc("Do not run any optimization passes"));

static cl::opt<bool>
DisableInternalize("disable-internalize",
                   cl::desc("Do not mark all symbols as internal"));

static cl::opt<bool>
StandardCompileOpts("std-compile-opts",
                   cl::desc("Include the standard compile time optimizations"));

static cl::opt<bool>
StandardLinkOpts("std-link-opts",
                 cl::desc("Include the standard link time optimizations"));

static cl::opt<bool>
OptLevelO1("O1",
           cl::desc("Optimization level 1. Similar to llvm-gcc -O1"));

static cl::opt<bool>
OptLevelO2("O2",
           cl::desc("Optimization level 2. Similar to llvm-gcc -O2"));

static cl::opt<bool>
OptLevelO3("O3",
           cl::desc("Optimization level 3. Similar to llvm-gcc -O3"));

static cl::opt<bool>
UnitAtATime("funit-at-a-time",
            cl::desc("Enable IPO. This is same as llvm-gcc's -funit-at-a-time"),
	    cl::init(true));

static cl::opt<bool>
DisableSimplifyLibCalls("disable-simplify-libcalls",
                        cl::desc("Disable simplify-libcalls"));

static cl::opt<bool>
Quiet("q", cl::desc("Obsolete option"), cl::Hidden);

static cl::alias
QuietA("quiet", cl::desc("Alias for -q"), cl::aliasopt(Quiet));

static cl::opt<bool>
AnalyzeOnly("analyze", cl::desc("Only perform analysis, no optimization"));

static cl::opt<std::string>
DefaultDataLayout("default-data-layout", 
          cl::desc("data layout string to use if not specified by module"),
          cl::value_desc("layout-string"), cl::init(""));

// ---------- Define Printers for module and function passes ------------
namespace {

struct CallGraphSCCPassPrinter : public CallGraphSCCPass {
  static char ID;
  const PassInfo *PassToPrint;
  CallGraphSCCPassPrinter(const PassInfo *PI) :
    CallGraphSCCPass(&ID), PassToPrint(PI) {}

  virtual bool runOnSCC(std::vector<CallGraphNode *>&SCC) {
    if (!Quiet) {
      outs() << "Printing analysis '" << PassToPrint->getPassName() << "':\n";

      for (unsigned i = 0, e = SCC.size(); i != e; ++i) {
        Function *F = SCC[i]->getFunction();
        if (F) {
          getAnalysisID<Pass>(PassToPrint).print(outs(), F->getParent());
        }
      }
    }
    // Get and print pass...
    return false;
  }

  virtual const char *getPassName() const { return "'Pass' Printer"; }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequiredID(PassToPrint);
    AU.setPreservesAll();
  }
};

char CallGraphSCCPassPrinter::ID = 0;

struct ModulePassPrinter : public ModulePass {
  static char ID;
  const PassInfo *PassToPrint;
  ModulePassPrinter(const PassInfo *PI) : ModulePass(&ID),
                                          PassToPrint(PI) {}

  virtual bool runOnModule(Module &M) {
    if (!Quiet) {
      outs() << "Printing analysis '" << PassToPrint->getPassName() << "':\n";
      getAnalysisID<Pass>(PassToPrint).print(outs(), &M);
    }

    // Get and print pass...
    return false;
  }

  virtual const char *getPassName() const { return "'Pass' Printer"; }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequiredID(PassToPrint);
    AU.setPreservesAll();
  }
};

char ModulePassPrinter::ID = 0;
struct FunctionPassPrinter : public FunctionPass {
  const PassInfo *PassToPrint;
  static char ID;
  FunctionPassPrinter(const PassInfo *PI) : FunctionPass(&ID),
                                            PassToPrint(PI) {}

  virtual bool runOnFunction(Function &F) {
    if (!Quiet) {
      outs() << "Printing analysis '" << PassToPrint->getPassName()
              << "' for function '" << F.getName() << "':\n";
    }
    // Get and print pass...
    getAnalysisID<Pass>(PassToPrint).print(outs(), F.getParent());
    return false;
  }

  virtual const char *getPassName() const { return "FunctionPass Printer"; }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequiredID(PassToPrint);
    AU.setPreservesAll();
  }
};

char FunctionPassPrinter::ID = 0;

struct LoopPassPrinter : public LoopPass {
  static char ID;
  const PassInfo *PassToPrint;
  LoopPassPrinter(const PassInfo *PI) :
    LoopPass(&ID), PassToPrint(PI) {}

  virtual bool runOnLoop(Loop *L, LPPassManager &LPM) {
    if (!Quiet) {
      outs() << "Printing analysis '" << PassToPrint->getPassName() << "':\n";
      getAnalysisID<Pass>(PassToPrint).print(outs(),
                                  L->getHeader()->getParent()->getParent());
    }
    // Get and print pass...
    return false;
  }

  virtual const char *getPassName() const { return "'Pass' Printer"; }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequiredID(PassToPrint);
    AU.setPreservesAll();
  }
};

char LoopPassPrinter::ID = 0;

struct BasicBlockPassPrinter : public BasicBlockPass {
  const PassInfo *PassToPrint;
  static char ID;
  BasicBlockPassPrinter(const PassInfo *PI)
    : BasicBlockPass(&ID), PassToPrint(PI) {}

  virtual bool runOnBasicBlock(BasicBlock &BB) {
    if (!Quiet) {
      outs() << "Printing Analysis info for BasicBlock '" << BB.getName()
             << "': Pass " << PassToPrint->getPassName() << ":\n";
    }

    // Get and print pass...
    getAnalysisID<Pass>(PassToPrint).print(outs(), BB.getParent()->getParent());
    return false;
  }

  virtual const char *getPassName() const { return "BasicBlockPass Printer"; }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequiredID(PassToPrint);
    AU.setPreservesAll();
  }
};

char BasicBlockPassPrinter::ID = 0;
inline void addPass(PassManager &PM, Pass *P) {
  // Add the pass to the pass manager...
  PM.add(P);

  // If we are verifying all of the intermediate steps, add the verifier...
  if (VerifyEach) PM.add(createVerifierPass());
}

/// AddOptimizationPasses - This routine adds optimization passes
/// based on selected optimization level, OptLevel. This routine
/// duplicates llvm-gcc behaviour.
///
/// OptLevel - Optimization Level
void AddOptimizationPasses(PassManager &MPM, FunctionPassManager &FPM,
                           unsigned OptLevel) {
  createStandardFunctionPasses(&FPM, OptLevel);

  llvm::Pass *InliningPass = 0;
  if (DisableInline) {
    // No inlining pass
  } else if (OptLevel) {
    unsigned Threshold = 200;
    if (OptLevel > 2)
      Threshold = 250;
    InliningPass = createFunctionInliningPass(Threshold);
  } else {
    InliningPass = createAlwaysInlinerPass();
  }
  createStandardModulePasses(&MPM, OptLevel,
                             /*OptimizeSize=*/ false,
                             UnitAtATime,
                             /*UnrollLoops=*/ OptLevel > 1,
                             !DisableSimplifyLibCalls,
                             /*HaveExceptions=*/ true,
                             InliningPass);
}

void AddStandardCompilePasses(PassManager &PM) {
  PM.add(createVerifierPass());                  // Verify that input is correct

  addPass(PM, createLowerSetJmpPass());          // Lower llvm.setjmp/.longjmp

  // If the -strip-debug command line option was specified, do it.
  if (StripDebug)
    addPass(PM, createStripSymbolsPass(true));

  if (DisableOptimizations) return;

  llvm::Pass *InliningPass = !DisableInline ? createFunctionInliningPass() : 0;

  // -std-compile-opts adds the same module passes as -O3.
  createStandardModulePasses(&PM, 3,
                             /*OptimizeSize=*/ false,
                             /*UnitAtATime=*/ true,
                             /*UnrollLoops=*/ true,
                             /*SimplifyLibCalls=*/ true,
                             /*HaveExceptions=*/ true,
                             InliningPass);
}

void AddStandardLinkPasses(PassManager &PM) {
  PM.add(createVerifierPass());                  // Verify that input is correct

  // If the -strip-debug command line option was specified, do it.
  if (StripDebug)
    addPass(PM, createStripSymbolsPass(true));

  if (DisableOptimizations) return;

  createStandardLTOPasses(&PM, /*Internalize=*/ !DisableInternalize,
                          /*RunInliner=*/ !DisableInline,
                          /*VerifyEach=*/ VerifyEach);
}

} // anonymous namespace


//===----------------------------------------------------------------------===//
// main for opt
//
int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);
  
  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  LLVMContext &Context = getGlobalContext();
  
  cl::ParseCommandLineOptions(argc, argv,
    "llvm .bc -> .bc modular optimizer and analysis printer\n");

  // Allocate a full target machine description only if necessary.
  // FIXME: The choice of target should be controllable on the command line.
  std::auto_ptr<TargetMachine> target;

  SMDiagnostic Err;

  // Load the input module...
  std::auto_ptr<Module> M;
  M.reset(ParseIRFile(InputFilename, Err, Context));

  if (M.get() == 0) {
    Err.Print(argv[0], errs());
    return 1;
  }

  // Figure out what stream we are supposed to write to...
  // FIXME: outs() is not binary!
  raw_ostream *Out = &outs();  // Default to printing to stdout...
  if (OutputFilename != "-") {
    if (NoOutput || AnalyzeOnly) {
      errs() << "WARNING: The -o (output filename) option is ignored when\n"
                "the --disable-output or --analyze options are used.\n";
    } else {
      // Make sure that the Output file gets unlinked from the disk if we get a
      // SIGINT
      sys::RemoveFileOnSignal(sys::Path(OutputFilename));

      std::string ErrorInfo;
      Out = new raw_fd_ostream(OutputFilename.c_str(), ErrorInfo,
                               raw_fd_ostream::F_Binary);
      if (!ErrorInfo.empty()) {
        errs() << ErrorInfo << '\n';
        delete Out;
        return 1;
      }
    }
  }

  // If the output is set to be emitted to standard out, and standard out is a
  // console, print out a warning message and refuse to do it.  We don't
  // impress anyone by spewing tons of binary goo to a terminal.
  if (!Force && !NoOutput && !AnalyzeOnly && !OutputAssembly)
    if (CheckBitcodeOutputToConsole(*Out, !Quiet))
      NoOutput = true;

  // Create a PassManager to hold and optimize the collection of passes we are
  // about to build...
  //
  PassManager Passes;

  // Add an appropriate TargetData instance for this module...
  TargetData *TD = 0;
  const std::string &ModuleDataLayout = M.get()->getDataLayout();
  if (!ModuleDataLayout.empty())
    TD = new TargetData(ModuleDataLayout);
  else if (!DefaultDataLayout.empty())
    TD = new TargetData(DefaultDataLayout);

  if (TD)
    Passes.add(TD);

  FunctionPassManager *FPasses = NULL;
  if (OptLevelO1 || OptLevelO2 || OptLevelO3) {
    FPasses = new FunctionPassManager(M.get());
    if (TD)
      FPasses->add(new TargetData(*TD));
  }

  // If the -strip-debug command line option was specified, add it.  If
  // -std-compile-opts was also specified, it will handle StripDebug.
  if (StripDebug && !StandardCompileOpts)
    addPass(Passes, createStripSymbolsPass(true));

  // Create a new optimization pass for each one specified on the command line
  for (unsigned i = 0; i < PassList.size(); ++i) {
    // Check to see if -std-compile-opts was specified before this option.  If
    // so, handle it.
    if (StandardCompileOpts &&
        StandardCompileOpts.getPosition() < PassList.getPosition(i)) {
      AddStandardCompilePasses(Passes);
      StandardCompileOpts = false;
    }

    if (StandardLinkOpts &&
        StandardLinkOpts.getPosition() < PassList.getPosition(i)) {
      AddStandardLinkPasses(Passes);
      StandardLinkOpts = false;
    }

    if (OptLevelO1 && OptLevelO1.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 1);
      OptLevelO1 = false;
    }

    if (OptLevelO2 && OptLevelO2.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 2);
      OptLevelO2 = false;
    }

    if (OptLevelO3 && OptLevelO3.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 3);
      OptLevelO3 = false;
    }

    const PassInfo *PassInf = PassList[i];
    Pass *P = 0;
    if (PassInf->getNormalCtor())
      P = PassInf->getNormalCtor()();
    else
      errs() << argv[0] << ": cannot create pass: "
             << PassInf->getPassName() << "\n";
    if (P) {
      PassKind Kind = P->getPassKind();
      addPass(Passes, P);

      if (AnalyzeOnly) {
        switch (Kind) {
        case PT_BasicBlock:
          Passes.add(new BasicBlockPassPrinter(PassInf));
          break;
        case PT_Loop:
          Passes.add(new LoopPassPrinter(PassInf));
          break;
        case PT_Function:
          Passes.add(new FunctionPassPrinter(PassInf));
          break;
        case PT_CallGraphSCC:
          Passes.add(new CallGraphSCCPassPrinter(PassInf));
          break;
        default:
          Passes.add(new ModulePassPrinter(PassInf));
          break;
        }
      }
    }

    if (PrintEachXForm)
      Passes.add(createPrintModulePass(&errs()));
  }

  // If -std-compile-opts was specified at the end of the pass list, add them.
  if (StandardCompileOpts) {
    AddStandardCompilePasses(Passes);
    StandardCompileOpts = false;
  }

  if (StandardLinkOpts) {
    AddStandardLinkPasses(Passes);
    StandardLinkOpts = false;
  }

  if (OptLevelO1)
    AddOptimizationPasses(Passes, *FPasses, 1);

  if (OptLevelO2)
    AddOptimizationPasses(Passes, *FPasses, 2);

  if (OptLevelO3)
    AddOptimizationPasses(Passes, *FPasses, 3);

  if (OptLevelO1 || OptLevelO2 || OptLevelO3) {
    FPasses->doInitialization();
    for (Module::iterator I = M.get()->begin(), E = M.get()->end();
         I != E; ++I)
      FPasses->run(*I);
  }

  // Check that the module is well formed on completion of optimization
  if (!NoVerify && !VerifyEach)
    Passes.add(createVerifierPass());

  // Write bitcode or assembly out to disk or outs() as the last step...
  if (!NoOutput && !AnalyzeOnly) {
    if (OutputAssembly)
      Passes.add(createPrintModulePass(Out));
    else
      Passes.add(createBitcodeWriterPass(*Out));
  }

  // Now that we have all of the passes ready, run them.
  Passes.run(*M.get());

  // Delete the raw_fd_ostream.
  if (Out != &outs())
    delete Out;
  return 0;
}

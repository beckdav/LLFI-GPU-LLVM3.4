
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include "llvm/IR/LegacyPassManager.h"
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "/usr/local/cuda-6.0/nvvm/include/nvvm.h"

#include <dlfcn.h>
#include <iostream>
#include <cstdio>
#include <list>
#include <string>
#include <sstream>
#include <vector>

#include <fstream>

using namespace llvm;
using namespace std;


#define LIBNVVM "libnvvm.so"

static void* libnvvm = NULL;

#define bind_lib(lib) \
	if (!libnvvm) \
{ \
	libnvvm = dlopen(lib, RTLD_NOW | RTLD_GLOBAL); \
	if (!libnvvm) \
	{ \
		fprintf(stderr, "Error loading %s: %s\n", lib, dlerror()); \
		abort(); \
	} \
}

#define bind_sym(handle, sym, retty, ...) \
	typedef retty (*sym##_func_t)(__VA_ARGS__); \
static sym##_func_t sym##_real = NULL; \
if (!sym##_real) \
{ \
	sym##_real = (sym##_func_t)dlsym(handle, #sym); \
	if (!sym##_real) \
	{ \
		fprintf(stderr, "Error loading %s: %s\n", #sym, dlerror()); \
		abort(); \
	} \
}

static Module* initial_module = NULL;

/////////////////////////////////////////////////////////////

static long bambooIndex = 1;

static void writeIrToFile(Module* module, const char* filePath){
	//errs() << "Dumping IR file ... \n";
	string err = "";
	raw_fd_ostream outputStream(filePath, err, llvm::sys::fs::F_None);
	WriteBitcodeToFile(module, outputStream);
	outputStream.flush();
}

static void indexInstruction(Instruction* BI){
	LLVMContext& C = BI->getContext();
	stringstream biString;
	biString << bambooIndex;
	string temp_str = biString.str();
	char const *biChar = temp_str.c_str();
	MDNode* N = MDNode::get(C, MDString::get(C, biChar));
	BI->setMetadata("bamboo_index", N);
	bambooIndex++;
}

static long getBambooIndex(Instruction* inst){
	MDNode *mdnode = inst->getMetadata("bamboo_index");
	if (mdnode) {
		//ConstantInt *cns_index = dyn_cast<ConstantInt>(mdnode->getOperand(0));
		//return cns_index->getSExtValue();
		string indexString = cast<MDString>(inst->getMetadata("bamboo_index")->getOperand(0))->getString().str();
		return atol(indexString.c_str());

	}
	return -1;
}

static void addProfileFunctionCall(Instruction* BI, Module* module){
	std::vector<Value*> checker_args(1);
	Value* indexValue = ConstantInt::get(Type::getInt64Ty(BI->getContext()), getBambooIndex(BI));
	checker_args[0] = indexValue;
	ArrayRef<Value*> args(checker_args);

	std::vector<Type*> checker_arg_types(1);
	checker_arg_types[0] = Type::getInt64Ty(module->getContext());
	ArrayRef<Type*> argsTypes(checker_arg_types);
	FunctionType* checker_type = FunctionType::get(Type::getVoidTy(BI->getContext()), argsTypes, false);
	Constant* checker_handler_c = module->getOrInsertFunction("bambooProfile", checker_type);
	Function* checker_handler = dyn_cast<Function>(checker_handler_c);
	CallInst::Create(checker_handler, args, "", BI);
	//errs() << "opcode:" << BI->getOpcode() << " index:" << getBambooIndex(BI) << "\n";
}

static void addProfileDump(Instruction* BI, Module* module){
	std::vector<Value*> checker_args(1);
	Value* indexValue = ConstantInt::get(Type::getInt64Ty(BI->getContext()), getBambooIndex(BI));
	checker_args[0] = indexValue;
	ArrayRef<Value*> args(checker_args);

	std::vector<Type*> checker_arg_types(1);
	checker_arg_types[0] = Type::getInt64Ty(module->getContext());
	ArrayRef<Type*> argsTypes(checker_arg_types);
	FunctionType* checker_type = FunctionType::get(Type::getVoidTy(BI->getContext()), argsTypes, false);
	Constant* checker_handler_c = module->getOrInsertFunction("bambooDump", checker_type);
	Function* checker_handler = dyn_cast<Function>(checker_handler_c);
	CallInst::Create(checker_handler, args, "", BI);
	//errs() << "opcode:" << BI->getOpcode() << " index:" << getBambooIndex(BI) << "\n";
}


/////////////////////////////////////////////////////////////

static void modifyModule(Module* module){
/*
	// Allocate fake const memory for parameters
	for (Module::iterator F = module->begin(), e = module->end(); F != e; F++){
		for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB){
			std::vector<Value*> args;
		        for(Function::arg_iterator ai = F->arg_begin(); ai != F->arg_end(); ++ai){
               			args.push_back(&*ai);
       			}			
			
			// Grab the first instruction and allocate for parameters
			Instruction* firstInst = F->getEntryBlock().getFirstNonPHI();
			
			for(int i=0; i<args.size(); i++){
				AllocaInst *paramloc = new AllocaInst( args[i]->getType(), "paramloc", firstInst );
				new StoreInst(args[i], paramloc, firstInst);
				LoadInst *loadParam = new LoadInst( paramloc, "loadParam", firstInst);
			
				// Replace parameters in all their uses
				std::list<User*> inst_uses;
				for (Value::use_iterator use_it = args[i]->use_begin(); use_it != args[i]->use_end(); ++use_it) {
					User *user = *use_it;
					inst_uses.push_back(user);
				}

				for (std::list<User*>::iterator use_it = inst_uses.begin(); use_it != inst_uses.end(); ++use_it) {
					User *user = *use_it;
					Instruction* nextUseInst = (dyn_cast<Instruction>(user));
					for(unsigned int j = 0; j < nextUseInst->getNumOperands(); j++){
						if(nextUseInst->getOperand(j) == args[i] && !isa<StoreInst>(nextUseInst)){
							nextUseInst->setOperand(j, loadParam);
						}
					}
				}
			}
			
			break;
		}	
	}
	
*/
	// Index
	for (Module::iterator F = module->begin(), e = module->end(); F != e; F++){
		for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB){
			for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI) {
				// Check if the instruction has return value
				std::list<User*> inst_uses;
				bool hasReturnValue = false;
				for (Value::use_iterator use_it = BI->use_begin(); use_it != BI->use_end(); ++use_it) {
					hasReturnValue = true;
				}

				// If so, we index it
				if(hasReturnValue == true && !isa<PHINode>(BI) && !isa<AllocaInst>(BI) ){
					indexInstruction(BI);
				}
			}
		}
	}

	// Profile
	// Parse index to inject: we use 'fiInstIndex' parameter
	std::vector<std::string> fiInstIndexVector;
	if(getenv("fiInstIndex")){
		std::string str(getenv("fiInstIndex"));
		std::string buf;
		std::stringstream ss(str);
		while(ss >> buf){
			fiInstIndexVector.push_back(buf);
		}
	}
	for (Module::iterator F = module->begin(), e = module->end(); F != e; F++){
		if( F->getName().find("bambooProfile") != std::string::npos ) continue;
		//errs() << F->getName() << "\n";

		for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB){
			for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI) {

				// Add profile function call
				if( getBambooIndex(BI) != -1 && BI->getOpcode() != 48 && BI->getOpcode() != 1 && BI->getOpcode() != 2){
					if(getenv("fiInstIndex")){
						for(int k=0; k<fiInstIndexVector.size(); k++){
							if(getBambooIndex(BI) == std::atol(fiInstIndexVector.at(k).c_str())){
								addProfileFunctionCall(BI, module);
							}
						}
					}else{
						addProfileFunctionCall(BI, module);
					}
				}

				//if(BI->getOpcode() == 1){
				//	addProfileDump(BI, module);
				//}

			}
		}
	}	

	errs() << "Profiling pass installed ... \n"; 

}

static bool called_compile = false;

nvvmResult nvvmAddModuleToProgram(nvvmProgram prog, const char *bitcode, size_t size, const char *name)
{
	bind_lib(LIBNVVM);
	bind_sym(libnvvm, nvvmAddModuleToProgram, nvvmResult, nvvmProgram, const char*, size_t, const char*);

	// Load module from bitcode.
	if (getenv("CICC_MODIFY_UNOPT_MODULE") && !initial_module)
	{

		string source = "";
		source.reserve(size);
		source.assign(bitcode, bitcode + size);
		MemoryBuffer *input = MemoryBuffer::getMemBuffer(source);
		string err;
		LLVMContext &context = getGlobalContext();
		initial_module = ParseBitcodeFile(input, context, &err);
		if (!initial_module)
			cerr << "Error parsing module bitcode : " << err;

		writeIrToFile(initial_module, "unopt_bamboo_before.ll");

		modifyModule(initial_module);

		// Dump unopt ir to file
		/*
		   ofstream irfile;
		   irfile.open("bamboo.ll");
		   irfile << source;
		   irfile.close();
		 */
		writeIrToFile(initial_module, "unopt_bamboo_after.ll");

		// Save module back into bitcode.
		SmallVector<char, 128> output;
		raw_svector_ostream outputStream(output);
		WriteBitcodeToFile(initial_module, outputStream);
		outputStream.flush();

		// Call real nvvmAddModuleToProgram
		return nvvmAddModuleToProgram_real(prog, output.data(), output.size(), name);
	}

	called_compile = true;

	// Call real nvvmAddModuleToProgram
	return nvvmAddModuleToProgram_real(prog, bitcode, size, name);	
}

#undef bind_lib

#define LIBC "libc.so.6"

static void* libc = NULL;

#define bind_lib(lib) \
	if (!libc) \
{ \
	libc = dlopen(lib, RTLD_NOW | RTLD_GLOBAL); \
	if (!libc) \
	{ \
		fprintf(stderr, "Error loading %s: %s\n", lib, dlerror()); \
		abort(); \
	} \
}

static Module* optimized_module = NULL;

struct tm *localtime(const time_t *timep)
{
	static bool localtime_first_call = true;

	bind_lib(LIBC);
	bind_sym(libc, localtime, struct tm*, const time_t*);

	if (getenv("CICC_MODIFY_OPT_MODULE") && called_compile && localtime_first_call)
	{
		localtime_first_call = false;

		writeIrToFile(optimized_module, "opt_bamboo_before.ll");

		modifyModule(optimized_module);

		writeIrToFile(optimized_module, "opt_bamboo_after.ll");

	}

	return localtime_real(timep);
}

#include <unistd.h>

#define MAX_SBRKS 16

struct sbrk_t { void* address; size_t size; };
static sbrk_t sbrks[MAX_SBRKS];
static int nsbrks = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

extern "C" void* malloc(size_t size)
{
	if (!size) return NULL;

	static bool __thread inside_malloc = false;

	if (!inside_malloc)
	{
		inside_malloc = true;

		bind_lib(LIBC);
		bind_sym(libc, malloc, void*, size_t);

		inside_malloc = false;

		void* result = malloc_real(size);

		if (called_compile && !optimized_module)
		{
			if (size == sizeof(Module)){
				optimized_module = (Module*)result;
			}
		}

		return result;
	}

	void* result = sbrk(size);
	if (nsbrks == MAX_SBRKS)
	{
		fprintf(stderr, "Out of sbrk tracking pool space\n");
		pthread_mutex_unlock(&mutex);
		abort();
	}
	pthread_mutex_lock(&mutex);
	sbrk_t s; s.address = result; s.size = size;
	sbrks[nsbrks++] = s;
	pthread_mutex_unlock(&mutex);

	return result;
}

extern "C" void* realloc(void* ptr, size_t size)
{
	bind_lib(LIBC);
	bind_sym(libc, realloc, void*, void*, size_t);

	for (int i = 0; i < nsbrks; i++)
		if (ptr == sbrks[i].address)
		{
			void* result = malloc(size);
#define MIN(a,b) (a) < (b) ? (a) : (b)
			memcpy(result, ptr, MIN(size, sbrks[i].size));
			return result;
		}

	return realloc_real(ptr, size);
}

extern "C" void free(void* ptr)
{
	bind_lib(LIBC);
	bind_sym(libc, free, void, void*);

	pthread_mutex_lock(&mutex);
	for (int i = 0; i < nsbrks; i++)
		if (ptr == sbrks[i].address) return;
	pthread_mutex_unlock(&mutex);

	free_real(ptr);
}





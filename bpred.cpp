#include <iostream>
#include <iomanip>
#include <fstream>
#include <assert.h>
#include <math.h>
#include <unistd.h>
#include "pin.H"

typedef unsigned int uint;

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "bpred.out", "specify file name for branch predictor output");
KNOB<UINT32> KnobM(KNOB_MODE_WRITEONCE, "pintool", "m", "0", "Global history size 0 <= m <= 16");
KNOB<UINT32> KnobN(KNOB_MODE_WRITEONCE, "pintool", "n", "1", "Counter width n >= 1");
KNOB<UINT32> KnobK(KNOB_MODE_WRITEONCE, "pintool", "k", "0", "Branch PC bits to use k, 0 <= k <= 16");

// These variables are my placeholders.  You may replace or modify 
// their usage, as needed.  However the final output format (see Fini, 
// below) should be unchanged.
uint total_bits = 0;
float accuracy = 0.0;
uint n = 0;
uint m = 0;
uint k = 0;
uint total_branches = 0;
uint total_taken = 0;
uint total_fallthru = 0;
uint total_predicts = 0;
uint *preds;
uint nlimit = 0;
uint threshold = 0;
unsigned short GHR = 0;
unsigned short GHR_mask = 0;
unsigned short addr_mask = 0;

void updateGHR(bool taken) {
    GHR = GHR << 1;
    if (taken)
        GHR|1;
}

bool predict_taken(unsigned short idx) {
    uint counter = preds[idx];

    if (counter < threshold) 
        return false;
    return true;
}

void updateCounter(unsigned short idx, bool taken) {
    if (taken) {
        if (preds[idx] < nlimit - 1)
            preds[idx] = preds[idx] + 1;
    } else {
        if (preds[idx] > 0)
            preds[idx] = preds[idx] - 1;
    }
}
// Invoked once per dynamic branch instruction
// pc: The address of the branch
// taken: Non zero if a branch is taken
VOID DoBranch(ADDRINT pc, BOOL taken) {
    total_branches++;
    unsigned short idx_addr = pc & addr_mask;
    unsigned short idx = ((GHR&GHR_mask)<<k)|idx_addr;
    bool pred_taken = predict_taken(idx);

    if (taken) {
        total_taken++;
        if (pred_taken)
            total_predicts++;
    } else {
        total_fallthru++;
        if (!pred_taken)
            total_predicts++;
    }
    // for efficiency updateCounter() and predict_taken() can be merged
    updateCounter(idx, taken);     
    updateGHR(taken);
}

// Called once per runtime image load
VOID Image(IMG img, VOID * v) {
    // find and instrument branches
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            RTN_Open(rtn);
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
	            if (INS_IsBranch(ins) && INS_HasFallThrough(ins)) {
	                INS_InsertCall( ins, IPOINT_BEFORE, (AFUNPTR)DoBranch, IARG_INST_PTR, IARG_BRANCH_TAKEN, IARG_END);
	            }
            }
            RTN_Close(rtn);
        }
    }
}

INT32 Usage() {
    cerr << "This pin tool simulates an (m,n,k) branch predictor." << endl;
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}

// Called once upon program exit
VOID Fini(int, VOID * v) {

    string filename;
    std::ofstream out;
    filename =  KnobOutputFile.Value();

    accuracy = (float)total_predicts / (float)total_branches;
    
    out.open(filename.c_str());
    out << "total_predicts " << total_predicts << endl;
    out << "m: " << KnobM.Value() << endl;
    out << "n: " << KnobN.Value() << endl;
    out << "k: " << KnobK.Value() << endl;
    out << "total_branches: " << total_branches << endl;
    out << "total_taken: " << total_taken << endl;
    out << "total_fallthru: " << total_fallthru << endl;
    out << "total_bits: " << total_bits << endl;
    out << "accuracy: " << accuracy << setprecision(3) << endl;

    out.close();
}

// Called once prior to program execution
int main(int argc, CHAR *argv[]) {
    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) {
        return Usage();
    }
    
    m = KnobM.Value();
    n = KnobN.Value();
    k = KnobK.Value();
    /* Space occupied by the Correlating Branch predictor */
    uint budget = 1 << (m + k);
    // could make it smaller since n is definitely less than 64 bits long
    preds = (uint *) calloc(budget, sizeof(uint));
    
    nlimit = 1 << n;
    threshold = nlimit/2;
    
    GHR_mask = (1<<m)-1;
    addr_mask = (1<<k)-1;
    
    total_bits = budget*n;

    IMG_AddInstrumentFunction(Image, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}


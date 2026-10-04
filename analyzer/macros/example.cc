#include <iostream>
#include <string>
#include <unistd.h>

#include "TApplication.h"
#include "TCanvas.h"
#include "TError.h"
#include "TH1D.h"
#include "TRandom.h"
#include "TROOT.h"

#include "Setting.h"
#include "TreeReader.h"

using std::cout;
using std::cerr;
using std::endl;
using std::string;

//********************************************************************//
int main(int argc, char** argv)
{
    gErrorIgnoreLevel = kError;
    gRandom->SetSeed(0);

    int ch;
    extern char* optarg;

    bool batch = false;
    string inputFile = "../rootfiles/run00125.root";

    while ((ch = getopt(argc, argv, "hf:b")) != -1) {
        switch (ch) {
        case 'f':
            inputFile = optarg; break;
        case 'b':
            gROOT->SetBatch(1);
            batch = true; break;
        case 'h':
            cout << "Usage: ./bin/example [options]" << endl;
            cout << "  -f <inputfile> : input ROOT file" << endl;
            cout << "  -b             : batch mode" << endl;
            cout << "  -h             : show this help" << endl;
            return 0;
        case '?':
            cout << "Unknown option. Type -h to see help." << endl; return 1;
        default:
            cout << "Type -h to see help." << endl; return 1;
        }
    }

    TApplication app("app", &argc, argv);

    Setting setting;
    setting.Initialize();

    TreeReader t(inputFile);

    if (!t.IsValid()) {
        cerr << "Failed to open ROOT tree: " << inputFile << endl;
        return 1;
    }

    const Long64_t entries = t.GetEntries();

    cout << "Input file        : " << inputFile << endl;
    cout << "Tree entries      : " << entries << endl;

    TH1D* h = new TH1D("h_qdc0_ch00", "", 512, 0.0, 4096.0);
    setting.Apply(h, "QDC0 Ch.0", "QDC [ch]", "Counts");

    for (Long64_t i = 0; i < entries; ++i) {
        if (!t.GetEntry(i)) {
            cerr << "Failed to read entry " << i << endl;
            continue;
        }

        const int value = t.qdc0[0];

        if (value >= 0) {
            h->Fill(value);
        }
    }

    cout << "Histogram entries : " << h->GetEntries() << endl;

    TCanvas* c = new TCanvas("c_qdc0", "QDC0 Ch.0", 900, 700);
    h->Draw();

    if (!batch) { app.Run(); }

    return 0;
}

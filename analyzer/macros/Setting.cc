#include "Setting.h"

#include <cmath>
#include <iostream>
#include <vector>

#include "TAxis.h"
#include "TColor.h"
#include "TF1.h"
#include "TGaxis.h"
#include "TGraph.h"
#include "TGraphAsymmErrors.h"
#include "TGraphErrors.h"
#include "TH1.h"
#include "TH2.h"
#include "TROOT.h"
#include "TStyle.h"

//********************************************************************//
void Setting::Initialize() const
{
    gROOT->SetStyle("Plain");

    gStyle->SetOptDate(0);

    // Histogram
    gStyle->SetHistFillStyle(3002);
    gStyle->SetHistFillColor(0);

    // Pad / frame
    gStyle->SetPadGridX(0);
    gStyle->SetPadGridY(0);
    gStyle->SetGridWidth(1);
    gStyle->SetFrameLineWidth(1);
    gStyle->SetLineWidth(1);

    // Axis divisions
    gStyle->SetNdivisions(505, "X");
    gStyle->SetNdivisions(505, "Y");
    gStyle->SetNdivisions(505, "Z");

    // Statistics box
    gStyle->SetOptStat(0);
    gStyle->SetStatFont(42);
    gStyle->SetStatFontSize(0.04);
    gStyle->SetStatTextColor(1);
    gStyle->SetStatBorderSize(1);

    // Pad margins
    gStyle->SetPadRightMargin(0.12);
    gStyle->SetPadLeftMargin(0.15);
    gStyle->SetPadTopMargin(0.10);
    gStyle->SetPadBottomMargin(0.13);

    // Title
    gStyle->SetTitleX(0.15);
    gStyle->SetTitleFontSize(0.050);
    gStyle->SetTitleFont(42, "");
    gStyle->SetTitleBorderSize(0);
    gStyle->SetTitleTextColor(1);
    gStyle->SetTitleStyle(0);

    // Labels
    gStyle->SetStripDecimals(kFALSE);
    gStyle->SetLabelFont(42, "XYZ");
    gStyle->SetLabelOffset(0.012, "X");
    gStyle->SetLegendFont(42);

    TGaxis::SetMaxDigits(5);

    // 2D color palette
    constexpr Int_t nRGBs = 5;
    constexpr Int_t nContours = 99;

    Double_t stops[nRGBs] = {0.00, 0.34, 0.61, 0.84, 1.00};
    Double_t red[nRGBs] = {0.00, 0.00, 0.87, 1.00, 0.51};
    Double_t green[nRGBs] = {0.00, 0.81, 1.00, 0.20, 0.00};
    Double_t blue[nRGBs] = {0.51, 1.00, 0.12, 0.00, 0.00};

    TColor::CreateGradientColorTable(nRGBs, stops, red, green, blue, nContours);
    gStyle->SetNumberContours(nContours);

    gROOT->ForceStyle();
}

//********************************************************************//
void Setting::ApplyAxis(TAxis* axis, const TString& title, double titleOffset) const
{
    if (!axis) {
        return;
    }

    axis->SetTitle(title);
    axis->CenterTitle();
    axis->SetTitleFont(42);
    axis->SetTitleOffset(titleOffset);
    axis->SetTitleSize(0.06);
    axis->SetLabelFont(42);
    axis->SetLabelOffset(0.01);
}

//********************************************************************//
void Setting::Apply(TH1* h, const TString& name, const TString& xname, const TString& yname, int lineColor, int fillStyle, int fillColor) const
{
    if (!h) {
        std::cerr << "Setting::Apply(TH1): null histogram" << std::endl;
        return;
    }

    h->SetTitle(name);
    h->SetLineColor(lineColor);
    h->SetLineWidth(1);
    h->SetFillStyle(fillStyle);
    h->SetFillColor(fillColor);
    h->SetTitleFont(42, "");
    h->SetTitleSize(0.04, "");

    ApplyAxis(h->GetXaxis(), xname, 0.90);
    ApplyAxis(h->GetYaxis(), yname, 1.10);
}

//********************************************************************//
void Setting::Apply(TH2* h, const TString& name, const TString& xname, const TString& yname, double minimum) const
{
    if (!h) {
        std::cerr << "Setting::Apply(TH2): null histogram" << std::endl;
        return;
    }

    h->SetTitle(name);
    h->SetMinimum(minimum);
    h->SetLineWidth(1);
    h->SetMarkerStyle(20);
    h->SetMarkerSize(1.0);
    h->SetMarkerColor(1);
    h->SetTitleFont(42, "");
    h->SetTitleSize(0.04, "");

    ApplyAxis(h->GetXaxis(), xname, 0.90);
    ApplyAxis(h->GetYaxis(), yname, 1.10);

    if (auto* zaxis = h->GetZaxis()) {
        zaxis->SetTitleFont(42);
        zaxis->SetTitleSize(0.06);
        zaxis->SetLabelFont(42);
        zaxis->SetLabelOffset(0.01);
    }
}

//********************************************************************//
void Setting::ApplyGraph(TGraph* gr, const TString& name, const TString& xname, const TString& yname, int lineColor, int lineWidth, int lineStyle, int markerColor, int markerStyle, double markerSize) const
{
    if (!gr) {
        std::cerr << "Setting::ApplyGraph: null graph" << std::endl;
        return;
    }

    gr->SetTitle(name);
    gr->SetName(name);

    ApplyAxis(gr->GetXaxis(), xname, 0.90);
    ApplyAxis(gr->GetYaxis(), yname, 1.10);

    gr->SetLineColor(lineColor);
    gr->SetLineWidth(lineWidth);
    gr->SetLineStyle(lineStyle);
    gr->SetMarkerColor(markerColor);
    gr->SetMarkerStyle(markerStyle);
    gr->SetMarkerSize(markerSize);
}

//********************************************************************//
void Setting::Apply(TGraph* gr, const TString& name, const TString& xname, const TString& yname, int lineColor, int lineWidth, int lineStyle, int markerColor, int markerStyle, double markerSize) const
{
    ApplyGraph(gr, name, xname, yname, lineColor, lineWidth, lineStyle, markerColor, markerStyle, markerSize);
}

//********************************************************************//
void Setting::Apply(TGraphErrors* gr, const TString& name, const TString& xname, const TString& yname, int lineColor, int lineWidth, int lineStyle, int markerColor, int markerStyle, double markerSize) const
{
    ApplyGraph(gr, name, xname, yname, lineColor, lineWidth, lineStyle, markerColor, markerStyle, markerSize);
}

//********************************************************************//
void Setting::Apply(TGraphAsymmErrors* gr, const TString& name, const TString& xname, const TString& yname, int lineColor, int lineWidth, int lineStyle, int markerColor, int markerStyle, double markerSize) const
{
    ApplyGraph(gr, name, xname, yname, lineColor, lineWidth, lineStyle, markerColor, markerStyle, markerSize);
}

//********************************************************************//
void Setting::Apply(TF1* f, int lineColor, int lineWidth, int lineStyle, int npx) const
{
    if (!f) {
        std::cerr << "Setting::Apply(TF1): null function" << std::endl;
        return;
    }

    f->SetLineColor(lineColor);
    f->SetLineWidth(lineWidth);
    f->SetLineStyle(lineStyle);
    f->SetNpx(npx);
}

//********************************************************************//
void Setting::BinLogX(TH2* h) const
{
    if (!h) {
        std::cerr << "Setting::BinLogX: null histogram" << std::endl;
        return;
    }

    TAxis* axis = h->GetXaxis();

    const int bins = axis->GetNbins();
    const double from = axis->GetXmin();
    const double to = axis->GetXmax();

    if (bins <= 0 || to <= from) {
        std::cerr << "Setting::BinLogX: invalid axis definition" << std::endl;
        return;
    }

    const double width = (to - from) / static_cast<double>(bins);
    std::vector<double> edges(static_cast<std::size_t>(bins) + 1);

    for (int i = 0; i <= bins; ++i) {
        edges[static_cast<std::size_t>(i)] = std::pow(10.0, from + static_cast<double>(i) * width);
    }

    axis->Set(bins, edges.data());
}

//********************************************************************//
void Setting::BinLogY(TH2* h) const
{
    if (!h) {
        std::cerr << "Setting::BinLogY: null histogram" << std::endl;
        return;
    }

    TAxis* axis = h->GetYaxis();

    const int bins = axis->GetNbins();
    const double from = axis->GetXmin();
    const double to = axis->GetXmax();

    if (bins <= 0 || to <= from) {
        std::cerr << "Setting::BinLogY: invalid axis definition" << std::endl;
        return;
    }

    const double width = (to - from) / static_cast<double>(bins);
    std::vector<double> edges(static_cast<std::size_t>(bins) + 1);

    for (int i = 0; i <= bins; ++i) {
        edges[static_cast<std::size_t>(i)] = std::pow(10.0, from + static_cast<double>(i) * width);
    }

    axis->Set(bins, edges.data());
}

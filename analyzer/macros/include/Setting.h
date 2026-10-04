#ifndef SETTING_H
#define SETTING_H

#include "TString.h"

class TAxis;
class TH1;
class TH2;
class TGraph;
class TGraphErrors;
class TGraphAsymmErrors;
class TF1;

class Setting
{
public:
    Setting() = default;
    ~Setting() = default;

    void Initialize() const;

    void Apply(TH1* h, const TString& name, const TString& xname, const TString& yname, int lineColor = 1, int fillStyle = 0, int fillColor = 0) const;

    void Apply(TH2* h, const TString& name, const TString& xname, const TString& yname, double minimum = 0.0) const;

    void Apply(TGraph* gr, const TString& name, const TString& xname, const TString& yname, int lineColor = 1, int lineWidth = 1, int lineStyle = 1, int markerColor = 1, int markerStyle = 20, double markerSize = 1.0) const;

    void Apply(TGraphErrors* gr, const TString& name, const TString& xname, const TString& yname, int lineColor = 1, int lineWidth = 1, int lineStyle = 1, int markerColor = 1, int markerStyle = 20, double markerSize = 1.0) const;

    void Apply(TGraphAsymmErrors* gr, const TString& name, const TString& xname, const TString& yname, int lineColor = 1, int lineWidth = 1, int lineStyle = 1, int markerColor = 1, int markerStyle = 20, double markerSize = 1.0) const;

    void Apply(TF1* f, int lineColor = 2, int lineWidth = 2, int lineStyle = 1, int npx = 1000) const;

    void BinLogX(TH2* h) const;
    void BinLogY(TH2* h) const;

private:
    void ApplyAxis(TAxis* axis, const TString& title, double titleOffset) const;

    void ApplyGraph(TGraph* gr, const TString& name, const TString& xname, const TString& yname, int lineColor, int lineWidth, int lineStyle, int markerColor, int markerStyle, double markerSize) const;
};

#endif

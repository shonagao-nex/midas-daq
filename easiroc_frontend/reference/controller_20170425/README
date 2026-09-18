================================================
===== Naruhiro Chikuma =========================
===== 2016-05-23 ===============================
================================================

=== Controller for EASIROC firmware (ver. 2016-05-23).

=== To check Ruby environment before running software.
$ ./install.sh

=== To run this program.
$ ./Controller.rb
(IP address may follow if it is changed from 192.168.10.16)
If plot program "hist" is made, histograms are automatically produced.
Outputs are put under the directory "data".


=== EASIROC/FPGA parameters are controlled by YAML cards.
- RegisterValue.yml
Any parameters of EASIROC slow control could be overwrite those in
DefaultRegisterValue. 
- InputDAC.yml
- PedestalSuppression.yml
- Calibration.yml

= Do not change the following cards.
- DefaultRegisterValue.yml
- RegisterAttribute.yml
- RegisterValueAlias.yml

= Do "make" to produce a program for conversion the .data file to .root file.

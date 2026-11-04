--------------------------------------------------------------------------------
--! @file   MMCM.vhd
--! @brief  Manage MMCM
--! @author Takehiro Shiozaki
--! @date   2014-06-11
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

library unisim;
use unisim.vcomponents.all;

entity MMCM is
    port(
        EXT_CLK : in std_logic;

        CLK_500M : out std_logic;
        CLK_250M_0 : out std_logic;
        CLK_250M_90 : out std_logic;
        CLK_250M_180 : out std_logic;
        CLK_250M_270 : out std_logic;
        CLK_125M : out std_logic;
        CLK_66M : out std_logic;
        CLK_25M : out std_logic;
        CLK_6M : out std_logic;

        LOCKED : out std_logic
    );
end MMCM;

architecture RTL of MMCM is
    signal ExtClkBuf : std_logic;

    signal ClkFbOut0 : std_logic;
    signal ClkFbOutBuf0 : std_logic;
    signal ClkFbOut1 : std_logic;

    signal Clk25MCascade : std_logic;
    signal Clk250M_0 : std_logic;
    signal Clk250M_90 : std_logic;
    signal Clk250M_180 : std_logic;
    signal Clk250M_270 : std_logic;
    signal Clk125M : std_logic;
    signal Clk500M : std_logic;
    signal Clk66M : std_logic;
    signal Clk25M : std_logic;
    signal Clk6M : std_logic;

    signal Locked0 : std_logic;
    signal Locked1 : std_logic;
begin

    MMCM_0: MMCME2_BASE
    generic map(
        BANDWIDTH            => "OPTIMIZED",
        CLKFBOUT_MULT_F      => 20.000,
        CLKFBOUT_PHASE       => 0.000,
        CLKIN1_PERIOD        => 20.000,
        CLKOUT0_DIVIDE_F     => 40.000,
        CLKOUT1_DIVIDE       => 4,
        CLKOUT2_DIVIDE       => 4,
        CLKOUT3_DIVIDE       => 4,
        CLKOUT4_DIVIDE       => 4,
        CLKOUT5_DIVIDE       => 8,
        CLKOUT6_DIVIDE       => 2,
        CLKOUT0_DUTY_CYCLE   => 0.500,
        CLKOUT1_DUTY_CYCLE   => 0.500,
        CLKOUT2_DUTY_CYCLE   => 0.500,
        CLKOUT3_DUTY_CYCLE   => 0.500,
        CLKOUT4_DUTY_CYCLE   => 0.500,
        CLKOUT5_DUTY_CYCLE   => 0.500,
        CLKOUT6_DUTY_CYCLE   => 0.500,
        CLKOUT0_PHASE        => 0.000,
        CLKOUT1_PHASE        => 0.000,
        CLKOUT2_PHASE        => 90.000,
        CLKOUT3_PHASE        => 180.000,
        CLKOUT4_PHASE        => 270.000,
        CLKOUT5_PHASE        => 0.000,
        CLKOUT6_PHASE        => 0.000,
        CLKOUT4_CASCADE      => FALSE,
        DIVCLK_DIVIDE        => 1,
        STARTUP_WAIT         => FALSE,
        REF_JITTER1          => 0.010
    )
    port map(
        CLKFBIN   => ClkFbOutBuf0,
        CLKFBOUT  => ClkFbOut0,
        CLKFBOUTB => open,
        CLKOUT0   => Clk25MCascade,
        CLKOUT0B  => open,
        CLKOUT1   => Clk250M_0,
        CLKOUT1B  => open,
        CLKOUT2   => Clk250M_90,
        CLKOUT2B  => open,
        CLKOUT3   => Clk250M_180,
        CLKOUT3B  => open,
        CLKOUT4   => Clk250M_270,
        CLKOUT5   => Clk125M,
        CLKOUT6   => Clk500M,
        CLKIN1    => ExtClkBuf,
        PWRDWN    => '0',
        RST       => '0',
        LOCKED    => Locked0
    );

    MMCM_1: MMCME2_BASE
    generic map(
        BANDWIDTH            => "OPTIMIZED",
        CLKFBOUT_MULT_F      => 24.000,
        CLKFBOUT_PHASE       => 0.000,
        CLKIN1_PERIOD        => 40.000,
        CLKOUT0_DIVIDE_F     => 24.000,
        CLKOUT1_DIVIDE       => 100,
        CLKOUT2_DIVIDE       => 9,
        CLKOUT0_DUTY_CYCLE   => 0.500,
        CLKOUT1_DUTY_CYCLE   => 0.500,
        CLKOUT2_DUTY_CYCLE   => 0.500,
        CLKOUT0_PHASE        => 0.000,
        CLKOUT1_PHASE        => 0.000,
        CLKOUT2_PHASE        => 0.000,
        CLKOUT4_CASCADE      => FALSE,
        DIVCLK_DIVIDE        => 1,
        STARTUP_WAIT         => FALSE,
        REF_JITTER1          => 0.010
    )
    port map(
        CLKFBIN   => ClkFbOut1,
        CLKFBOUT  => ClkFbOut1,
        CLKFBOUTB => open,
        CLKOUT0   => Clk25M,
        CLKOUT0B  => open,
        CLKOUT1   => Clk6M,
        CLKOUT1B  => open,
        CLKOUT2   => Clk66M,
        CLKOUT2B  => open,
        CLKOUT3   => open,
        CLKOUT3B  => open,
        CLKOUT4   => open,
        CLKOUT5   => open,
        CLKOUT6   => open,
        CLKIN1    => Clk25MCascade,
        PWRDWN    => '0',
        RST       => '0',
        LOCKED    => Locked1
    );

    IBUFG_EXT_CLK: IBUFG
    port map(
        I => EXT_CLK,
        O => ExtClkBuf
    );

    BUFG_CLK_FB_0: BUFG
    port map(
        I => ClkFbOut0,
        O => ClkFbOutBuf0
    );

    BUFG_CLK_250M_0: BUFG
    port map(
        I => Clk250M_0,
        O => CLK_250M_0
    );

    BUFG_CLK_250M_90: BUFG
    port map(
        I => Clk250M_90,
        O => CLK_250M_90
    );

    BUFG_CLK_250M_180: BUFG
    port map(
        I => Clk250M_180,
        O => CLK_250M_180
    );

    BUFG_CLK_250M_270: BUFG
    port map(
        I => Clk250M_270,
        O => CLK_250M_270
    );

    BUFG_CLK_125M: BUFG
    port map(
        I => Clk125M,
        O => CLK_125M
    );

    BUFG_CLK_500M: BUFG
    port map(
        I => Clk500M,
        O => CLK_500M
    );

    BUFG_CLK_25M: BUFG
    port map(
        I => Clk25M,
        O => CLK_25M
    );

    BUFG_CLK_6M: BUFG
    port map(
        I => Clk6M,
        O => CLK_6M
    );

    BUFG_CLK_66M: BUFG
    port map(
        I => Clk66M,
        O => CLK_66M
    );

    LOCKED <= Locked0 and Locked1;
end RTL;



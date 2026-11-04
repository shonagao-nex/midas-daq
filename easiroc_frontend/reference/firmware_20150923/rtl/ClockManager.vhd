--------------------------------------------------------------------------------
--! @file   ClockManager.vhd
--! @brief  generate Clocks from External Clock
--! @author Takehiro Shiozaki
--! @date   2014-06-12
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

library unisim;
use unisim.vcomponents.all;

entity ClockManager is
    port(
        EXT_CLK : in  std_logic;              -- 50MHz
        RESET : in  std_logic;
        LOCKED : out std_logic;               -- PLL Locked
        SITCP_CLK : out  std_logic;           -- 25MHz
        SLOWCONTROL_CLK : out  std_logic;     -- 6MHz
        ADC_CLK : out  std_logic;             -- 6MHz
        AD9220_CLK : out  std_logic;          -- 3MHz (Synchronized with ADC_CLK)
        AD9220_CLK_OUT : out std_logic;       -- 3MHz for AD9220
        AD9220_CLK_ENABLE : in std_logic;     -- AD9220 CLK Enable
        TDC_CLK : out std_logic;              -- 125MHz
        TDC_SAMPLING_CLK_0 : out std_logic;   -- 250MHz 0degree
        TDC_SAMPLING_CLK_90 : out std_logic;  -- 250MHz 90degree
        TDC_SAMPLING_CLK_180 : out std_logic; -- 250MHz 180degree
        TDC_SAMPLING_CLK_270 : out std_logic; -- 250MHz 270degree
        FAST_CLK : out std_logic;             -- 500MHz
        SPI_CLK : out std_logic               -- 66MHz
    );
end ClockManager;

architecture RTL of ClockManager is

    component MMCM is
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
    end component;

    signal Clk6M : std_logic;
    signal Clk3M : std_logic;

    signal Clk6MBuf : std_logic;

begin

    MMCM_0: MMCM
    port map(
        EXT_CLK => EXT_CLK,
        CLK_500M => FAST_CLK,
        CLK_250M_0 => TDC_SAMPLING_CLK_0,
        CLK_250M_90 => TDC_SAMPLING_CLK_90,
        CLK_250M_180 => TDC_SAMPLING_CLK_180,
        CLK_250M_270 => TDC_SAMPLING_CLK_270,
        CLK_125M => TDC_CLK,
        CLK_66M => SPI_CLK,
        CLK_25M => SITCP_CLK,
        CLK_6M => Clk6M,
        LOCKED => LOCKED
    );

    process(Clk6M, RESET)
    begin
        if(RESET = '1') then
            Clk3M <= '0';
        elsif(Clk6M'event and Clk6M = '1') then
            Clk3M <= not Clk3M;
        end if;
    end process;

    BUFG_6M: BUFG
    port map(
        O => Clk6MBuf,
        I => Clk6M
    );

    SLOWCONTROL_CLK <= Clk6MBuf;
    ADC_CLK <= Clk6MBuf;

    BUFG_AD9220_CLK: BUFG
    port map(
        O => AD9220_CLK,
        I => Clk3M
    );

    BUFG_AD9220_CLK_OUT: BUFGCE
    port map(
        O => AD9220_CLK_OUT,
        CE => AD9220_CLK_ENABLE,
        I => Clk3M
    );

end RTL;


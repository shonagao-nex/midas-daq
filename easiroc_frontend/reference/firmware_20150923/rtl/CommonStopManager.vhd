--------------------------------------------------------------------------------
--! @file   CommonStopManager.vhd
--! @brief  Manage COMMON_STOP signal
--! @author Takehiro Shiozaki
--! @date   2014-06-10
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity CommonStopManager is
    port(
        CLK_0 : in std_logic;   -- 250MHz 0degree
        CLK_90 : in std_logic;  -- 250MHz 90degree
        CLK_180 : in std_logic; -- 250MHz 180degree
        CLK_270 : in std_logic; -- 250MHz 270degree
        TDC_CLK : in std_logic; -- 125MHz

        COMMON_STOP : in std_logic;
        COARSE_COUNT : in std_logic_vector(42 downto 0);

        COMMON_STOP_OUT : out std_logic;
        COMMON_STOP_COUNT : out std_logic_vector(45 downto 0)
    );
end CommonStopManager;

architecture RTL of CommonStopManager is
    component MHTDC_Counter is
        port(
            CLK_0 : in std_logic;   -- 250MHz 0degree
            CLK_90 : in std_logic;  -- 250MHz 90degree
            CLK_180 : in std_logic; -- 250MHz 180degree
            CLK_270 : in std_logic; -- 250MHz 270degree
            TDC_CLK : in std_logic; -- 125MHz

            DIN : in std_logic;
            COARSE_COUNT : in std_logic_vector(42 downto 0);
            COUNT : out std_logic_vector(45 downto 0);
            HIT_FIND : out std_logic
        );
    end component;
begin

    MHTDC_Counter_0: MHTDC_Counter
    port map(
        CLK_0 => CLK_0,
        CLK_90 => CLK_90,
        CLK_180 => CLK_180,
        CLK_270 => CLK_270,
        TDC_CLK => TDC_CLK,
        DIN => COMMON_STOP,
        COARSE_COUNT => COARSE_COUNT,
        COUNT => COMMON_STOP_COUNT,
        HIT_FIND => COMMON_STOP_OUT
    );
end RTL;

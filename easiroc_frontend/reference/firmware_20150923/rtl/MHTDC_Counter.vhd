--------------------------------------------------------------------------------
--! @file   MHTDC_Counter.vhd
--! @brief  MHTDC Counter
--! @author Takehiro Shiozaki
--! @date   2014-06-07
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity MHTDC_Counter is
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
end MHTDC_Counter;

architecture RTL of MHTDC_Counter is
    component FineCounter is
        port (
            CLK_0 : in std_logic;
            CLK_90 : in std_logic;
            CLK_180 : in std_logic;
            CLK_270 : in std_logic;

            DIN : in std_logic;
            DOUT : out std_logic_vector(3 downto 0)
        );
    end component;

    component FineCounterDecoder is
        port (
            CLK_0 : in std_logic;   -- 250MHz
            TDC_CLK : in std_logic; -- 125MHz

            DIN : in std_logic_vector(3 downto 0);

            SEMI_FINE_COUNT : out std_logic;
            FINE_COUNT : out std_logic_vector(1 downto 0);
            HIT_FIND : out std_logic
        );
    end component;

    signal FineCounterDout : std_logic_vector(3 downto 0);
    signal SemiFineCount : std_logic;
    signal FineCount : std_logic_vector(1 downto 0);
begin
    FineCounter_0 : FineCounter
    port map(
        CLK_0 => CLK_0,
        CLK_90 => CLK_90,
        CLK_180 => CLK_180,
        CLK_270 => CLK_270,
        DIN => DIN,
        DOUT => FineCounterDout
    );

    FineCounterDecoder_0 : FineCounterDecoder
    port map(
        CLK_0 => CLK_0,
        TDC_CLK => TDC_CLK,
        DIN => FineCounterDout,
        SEMI_FINE_COUNT => SemiFineCount,
        FINE_COUNT => FineCount,
        HIT_FIND => HIT_FIND
    );

    COUNT <= COARSE_COUNT & SemiFineCount & FineCount;
end RTL;

--------------------------------------------------------------------------------
--! @file   MHTDC.vhd
--! @brief  Multi Hit TDC
--! @author Takehiro Shiozaki
--! @date   2014-06-11
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity MHTDC is
    generic (
        G_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
    );
    port(
        TDC_CLK : in  std_logic; -- 125MHz
        CLK_0 : in std_logic;    -- 250MHz 0degree
        CLK_90 : in std_logic;   -- 250MHz 90degree
        CLK_180 : in std_logic;  -- 250MHz 180degree
        CLK_270 : in std_logic;  -- 250MHz 270degree
        SITCP_CLK : in std_logic;
        RESET : in  std_logic;

        DIN : in std_logic_vector(63 downto 0);
        COMMON_STOP : in std_logic;
        FAST_CLEAR : in std_logic;

        -- RBCP Interface
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic;

        BUSY : out std_logic;

        DOUT_L : out std_logic_vector(19 downto 0);
        RADDR_L : in std_logic_vector(10 downto 0);
        RCOMP_L : in std_logic;
        EMPTY_L : out std_logic;

        DOUT_T : out std_logic_vector(19 downto 0);
        RADDR_T : in std_logic_vector(10 downto 0);
        RCOMP_T : in std_logic;
        EMPTY_T : out std_logic
    );
end MHTDC;

architecture RTL of MHTDC is

    component MHTDC_Core is
        generic (
            G_TIME_WINDOW_REGISTER_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
        );
        port(
            TDC_CLK : in std_logic;     -- 125MHz
            CLK_0 : in std_logic;   -- 250MHz 0degree
            CLK_90 : in std_logic;  -- 250MHz 90degree
            CLK_180 : in std_logic; -- 250MHz 180degree
            CLK_270 : in std_logic; -- 250MHz 270degree
            RESET : in std_logic;

            DIN : in std_logic_vector(63 downto 0);
            COMMON_STOP : in std_logic;
            FAST_CLEAR : in std_logic;

            -- RBCP Interface
            SITCP_CLK : in std_logic;
            RBCP_ACT : in std_logic;
            RBCP_ADDR : in std_logic_vector(31 downto 0);
            RBCP_WE : in std_logic;
            RBCP_WD : in std_logic_vector(7 downto 0);
            RBCP_ACK : out std_logic;

            -- Eventbuffer interface(Leading)
            DOUT_L : out std_logic_vector(19 downto 0);
            ADDR_L : out std_logic_vector(10 downto 0);
            WE_L : out std_logic;
            FULL_L : in std_logic;
            WCOMP_L : out std_logic;
            DEC_WPTR_L : out std_logic;

            -- Eventbuffer interface(Trailing)
            DOUT_T : out std_logic_vector(19 downto 0);
            ADDR_T : out std_logic_vector(10 downto 0);
            WE_T : out std_logic;
            FULL_T : in std_logic;
            WCOMP_T : out std_logic;
            DEC_WPTR_T : out std_logic;

            BUSY : out std_logic
        );
    end component;

    component DoubleBuffer
    generic(
        G_WIDTH : integer;
        G_DEPTH : integer
    );
    port(
        RESET : in std_logic;
        WCLK : in std_logic;
        DIN : in std_logic_vector(G_WIDTH - 1 downto 0);
        WADDR : in std_logic_vector(G_DEPTH - 1 downto 0);
        WE : in std_logic;
        WCOMP : in std_logic;
        DEC_WPTR : in std_logic;
        RCLK : in std_logic;
        RADDR : in std_logic_vector(G_DEPTH - 1 downto 0);
        RCOMP : in std_logic;
        FULL : out std_logic;
        DOUT : out std_logic_vector(G_WIDTH - 1 downto 0);
        EMPTY : out std_logic
    );
    end component;

    signal CoreDoutLeading : std_logic_vector(19 downto 0);
    signal CoreAddrLeading : std_logic_vector(10 downto 0);
    signal CoreWeLeading : std_logic;
    signal EventBufferFullLeading : std_logic;
    signal CoreWcompLeading : std_logic;
    signal CoreDecWptrLeading : std_logic;
    signal CoreDoutTrailing : std_logic_vector(19 downto 0);
    signal CoreAddrTrailing : std_logic_vector(10 downto 0);
    signal CoreWeTrailing : std_logic;
    signal EventBufferFullTrailing : std_logic;
    signal CoreWcompTrailing : std_logic;
    signal CoreDecWptrTrailing : std_logic;
begin

    MHTDC_Core_0: MHTDC_Core
    generic map(
        G_TIME_WINDOW_REGISTER_ADDRESS => G_TIME_WINDOW_REGISTER_ADDRESS
    )
    port map(
        TDC_CLK => TDC_CLK,
        CLK_0 => CLK_0,
        CLK_90 => CLK_90,
        CLK_180 => CLK_180,
        CLK_270 => CLK_270,
        RESET => RESET,
        DIN => DIN,
        COMMON_STOP => COMMON_STOP,
        FAST_CLEAR => FAST_CLEAR,
        SITCP_CLK => SITCP_CLK,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RBCP_ACK,
        DOUT_L => CoreDoutLeading,
        ADDR_L => CoreAddrLeading,
        WE_L => CoreWeLeading,
        FULL_L => EventBufferFullLeading,
        WCOMP_L => CoreWcompLeading,
        DEC_WPTR_L => CoreDecWptrLeading,
        DOUT_T => CoreDoutTrailing,
        ADDR_T => CoreAddrTrailing,
        WE_T => CoreWeTrailing,
        FULL_T => EventBufferFullTrailing,
        WCOMP_T => CoreWcompTrailing,
        DEC_WPTR_T => CoreDecWptrTrailing,
        BUSY => BUSY
    );

    MHTDC_EventBuffer_Leading: DoubleBuffer
    generic map(
        G_WIDTH => 20,
        G_DEPTH => 11)
    port map(
        RESET => RESET,
        WCLK => TDC_CLK,
        DIN => CoreDoutLeading,
        WADDR => CoreAddrLeading,
        WE => CoreWeLeading,
        WCOMP => CoreWcompLeading,
        DEC_WPTR => CoreDecWptrLeading,
        FULL => EventBufferFullLeading,
        RCLK => SITCP_CLK,
        DOUT => DOUT_L,
        RADDR => RADDR_L,
        RCOMP => RCOMP_L,
        EMPTY => EMPTY_L
    );

    MHTDC_EventBuffer_Trailing: DoubleBuffer
    generic map(
        G_WIDTH => 20,
        G_DEPTH => 11)
    port map(
        RESET => RESET,
        WCLK => TDC_CLK,
        DIN => CoreDoutTrailing,
        WADDR => CoreAddrTrailing,
        WE => CoreWeTrailing,
        WCOMP => CoreWcompTrailing,
        DEC_WPTR => CoreDecWptrTrailing,
        FULL => EventBufferFullTrailing,
        RCLK => SITCP_CLK,
        DOUT => DOUT_T,
        RADDR => RADDR_T,
        RCOMP => RCOMP_T,
        EMPTY => EMPTY_T
    );
end RTL;


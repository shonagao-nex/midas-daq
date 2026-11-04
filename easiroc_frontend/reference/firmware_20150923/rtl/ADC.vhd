--------------------------------------------------------------------------------
--! @file   ADC.vhd
--! @brief  Analog to Digital Converter
--! @author Takehiro Shiozaki
--! @date   2013-11-11
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity ADC is
    generic(
        G_PEDESTAL_SUPPRESSION_ADDR : std_logic_vector(31 downto 0) := X"00000000"
    );
    port (
        SITCP_CLK : in  std_logic;
        ADC_CLK : in std_logic;
        AD9220_CLK : in std_logic;
        RESET : in  std_logic;

        -- Control interface
        TRIGGER : in std_logic;
        FAST_CLEAR : in std_logic;
        BUSY : out std_logic;
        AD9220_CLK_ENABLE : out std_logic;

        -- EASIROC ReadRegister interface
        CLK_READ1 : out std_logic;
        RSTB_READ1 : out std_logic;
        SRIN_READ1 : out std_logic;
        CLK_READ2 : out std_logic;
        RSTB_READ2 : out std_logic;
        SRIN_READ2 : out std_logic;

        -- AD9220 interface
        ADC_DATA_HG1 : in std_logic_vector(11 downto 0);
        ADC_OTR_HG1 : in std_logic;
        ADC_DATA_LG1 : in std_logic_vector(11 downto 0);
        ADC_OTR_LG1 : in std_logic;
        ADC_DATA_HG2 : in std_logic_vector(11 downto 0);
        ADC_OTR_HG2 : in std_logic;
        ADC_DATA_LG2 : in std_logic_vector(11 downto 0);
        ADC_OTR_LG2 : in std_logic;

        -- Gatherer interface
        ADC_DOUT_HG1 : out  std_logic_vector (20 downto 0);
        ADC_RADDR_HG1 : in  std_logic_vector (5 downto 0);
        ADC_RCOMP_HG1 : in  std_logic;
        ADC_EMPTY_HG1 : out  std_logic;
        ADC_DOUT_LG1 : out  std_logic_vector (20 downto 0);
        ADC_RADDR_LG1 : in  std_logic_vector (5 downto 0);
        ADC_RCOMP_LG1 : in  std_logic;
        ADC_EMPTY_LG1 : out  std_logic;
        ADC_DOUT_HG2 : out  std_logic_vector (20 downto 0);
        ADC_RADDR_HG2 : in  std_logic_vector (5 downto 0);
        ADC_RCOMP_HG2 : in  std_logic;
        ADC_EMPTY_HG2 : out  std_logic;
        ADC_DOUT_LG2 : out  std_logic_vector (20 downto 0);
        ADC_RADDR_LG2 : in  std_logic_vector (5 downto 0);
        ADC_RCOMP_LG2 : in  std_logic;
        ADC_EMPTY_LG2 : out  std_logic;

        -- RBCP interface
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic
    );
end ADC;

architecture RTL of ADC is

    component ADC_Controller
        port(
            ADC_CLK : in std_logic;
            AD9220_CLK : in std_logic;
            RESET : in std_logic;
            TRIGGER : in std_logic;
            FAST_CLEAR : in std_logic;
            AD9220_CLK_ENABLE : out std_logic;
            CORE_BUSY0 : in std_logic;
            CORE_BUSY1 : in std_logic;
            CORE_BUSY2 : in std_logic;
            CORE_BUSY3 : in std_logic;
            BUSY : out std_logic;
            CORE_START : out std_logic;
            CLK_READ : out std_logic;
            RSTB_READ : out std_logic;
            SRIN_READ : out std_logic
        );
    end component;

    component ADC_Core
        generic(
            G_IS_LAST_CHANNEL : std_logic;
            G_IS_LOW_GAIN : std_logic;
            G_PEDESTAL_SUPPRESSION_ADDR : std_logic_vector(31 downto 0)
        );
        port(
            ADC_CLK : in  std_logic;
            RESET : in  std_logic;
            START : in  std_logic;
            FAST_CLEAR : in std_logic;
            BUSY : out  std_logic;
            ADC_DATA : in  std_logic_vector(11 downto 0);
            ADC_OTR : in  std_logic;
            DOUT : out  std_logic_vector(20 downto 0);
            ADDR : out  std_logic_vector(5 downto 0);
            WE : out  std_logic;
            FULL : in  std_logic;
            WCOMP : out  std_logic;
            DEC_WPTR : out std_logic;
            RBCP_CLK : in std_logic;
            RBCP_ACT : in std_logic;
            RBCP_ADDR : in std_logic_vector(31 downto 0);
            RBCP_WE : in std_logic;
            RBCP_WD : in std_logic_vector(7 downto 0);
            RBCP_ACK : out std_logic
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

    signal CoreStart : std_logic;
    signal CoreBusy0 : std_logic;
    signal CoreBusy1 : std_logic;
    signal CoreBusy2 : std_logic;
    signal CoreBusy3 : std_logic;
    signal ClkRead : std_logic;
    signal RstbRead : std_logic;
    signal SrinRead : std_logic;

    signal CoreDoutHg1 : std_logic_vector(20 downto 0);
    signal CoreAddrHg1 : std_logic_vector(5 downto 0);
    signal CoreWeHg1 : std_logic;
    signal CoreWcompHg1 : std_logic;
    signal CoreDecWptrHg1 : std_logic;
    signal EventBufferFullHg1 : std_logic;

    signal CoreDoutLg1 : std_logic_vector(20 downto 0);
    signal CoreAddrLg1 : std_logic_vector(5 downto 0);
    signal CoreWeLg1 : std_logic;
    signal CoreWcompLg1 : std_logic;
    signal CoreDecWptrLg1 : std_logic;
    signal EventBufferFullLg1 : std_logic;

    signal CoreDoutHg2 : std_logic_vector(20 downto 0);
    signal CoreAddrHg2 : std_logic_vector(5 downto 0);
    signal CoreWeHg2 : std_logic;
    signal CoreWcompHg2 : std_logic;
    signal CoreDecWptrHg2 : std_logic;
    signal EventBufferFullHg2 : std_logic;

    signal CoreDoutLg2 : std_logic_vector(20 downto 0);
    signal CoreAddrLg2 : std_logic_vector(5 downto 0);
    signal CoreWeLg2 : std_logic;
    signal CoreWcompLg2 : std_logic;
    signal CoreDecWptrLg2 : std_logic;
    signal EventBufferFullLg2 : std_logic;

    signal RbcpAckCoreHg1 : std_logic;
    signal RbcpAckCoreHg2 : std_logic;
    signal RbcpAckCoreLg1 : std_logic;
    signal RbcpAckCoreLg2 : std_logic;

begin

    ADC_Controller_0: ADC_Controller
    port map(
        ADC_CLK => ADC_CLK,
        AD9220_CLK => AD9220_CLK,
        RESET => RESET,
        TRIGGER => TRIGGER,
        FAST_CLEAR => FAST_CLEAR,
        BUSY => BUSY,
        AD9220_CLK_ENABLE => AD9220_CLK_ENABLE,
        CORE_START => CoreStart,
        CORE_BUSY0 => CoreBusy0,
        CORE_BUSY1 => CoreBusy1,
        CORE_BUSY2 => CoreBusy2,
        CORE_BUSY3 => CoreBusy3,
        CLK_READ => ClkRead,
        RSTB_READ => RstbRead,
        SRIN_READ => SrinRead
    );

    CLK_READ1 <= ClkRead;
    CLK_READ2 <= ClkRead;
    RSTB_READ1 <= RstbRead;
    RSTB_READ2 <= RstbRead;
    SRIN_READ1 <= SrinRead;
    SRIN_READ2 <= SrinRead;

    ADC_Core_HG1: ADC_Core
    generic map(
        G_IS_LAST_CHANNEL => '0',
        G_IS_LOW_GAIN => '0',
        G_PEDESTAL_SUPPRESSION_ADDR => G_PEDESTAL_SUPPRESSION_ADDR
    )
    port map(
        ADC_CLK => ADC_CLK,
        RESET => RESET,
        START => CoreStart,
        FAST_CLEAR => FAST_CLEAR,
        BUSY => CoreBusy0,
        ADC_DATA => ADC_DATA_HG1,
        ADC_OTR => ADC_OTR_HG1,
        DOUT => CoreDoutHg1,
        ADDR => CoreAddrHg1,
        WE => CoreWeHg1,
        FULL => EventBufferFullHg1,
        WCOMP => CoreWcompHg1,
        DEC_WPTR => CoreDecWptrHg1,
        RBCP_CLK => SITCP_CLK,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RbcpAckCoreHg1
    );

    ADC_Core_LG1: ADC_Core
    generic map(
        G_IS_LAST_CHANNEL => '0',
        G_IS_LOW_GAIN => '1',
        G_PEDESTAL_SUPPRESSION_ADDR => G_PEDESTAL_SUPPRESSION_ADDR + 128
    )
    port map(
        ADC_CLK => ADC_CLK,
        RESET => RESET,
        START => CoreStart,
        FAST_CLEAR => FAST_CLEAR,
        BUSY => CoreBusy1,
        ADC_DATA => ADC_DATA_LG1,
        ADC_OTR => ADC_OTR_LG1,
        DOUT => CoreDoutLg1,
        ADDR => CoreAddrLg1,
        WE => CoreWeLg1,
        FULL => EventBufferFullLg1,
        WCOMP => CoreWcompLg1,
        DEC_WPTR => CoreDecWptrLg1,
        RBCP_CLK => SITCP_CLK,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RbcpAckCoreLg1
    );

    ADC_Core_HG2: ADC_Core
    generic map(
        G_IS_LAST_CHANNEL => '1',
        G_IS_LOW_GAIN => '0',
        G_PEDESTAL_SUPPRESSION_ADDR => G_PEDESTAL_SUPPRESSION_ADDR + 64
    )
    port map(
        ADC_CLK => ADC_CLK,
        RESET => RESET,
        START => CoreStart,
        FAST_CLEAR => FAST_CLEAR,
        BUSY => CoreBusy2,
        ADC_DATA => ADC_DATA_HG2,
        ADC_OTR => ADC_OTR_HG2,
        DOUT => CoreDoutHg2,
        ADDR => CoreAddrHg2,
        WE => CoreWeHg2,
        FULL => EventBufferFullHg2,
        WCOMP => CoreWcompHg2,
        DEC_WPTR => CoreDecWptrHg2,
        RBCP_CLK => SITCP_CLK,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RbcpAckCoreHg2
    );

    ADC_Core_LG2: ADC_Core
    generic map(
        G_IS_LAST_CHANNEL => '1',
        G_IS_LOW_GAIN => '1',
        G_PEDESTAL_SUPPRESSION_ADDR => G_PEDESTAL_SUPPRESSION_ADDR + 192
    )
    port map(
        ADC_CLK => ADC_CLK,
        RESET => RESET,
        START => CoreStart,
        FAST_CLEAR => FAST_CLEAR,
        BUSY => CoreBusy3,
        ADC_DATA => ADC_DATA_LG2,
        ADC_OTR => ADC_OTR_LG2,
        DOUT => CoreDoutLg2,
        ADDR => CoreAddrLg2,
        WE => CoreWeLg2,
        FULL => EventBufferFullLg2,
        WCOMP => CoreWcompLg2,
        DEC_WPTR => CoreDecWptrLg2,
        RBCP_CLK => SITCP_CLK,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RbcpAckCoreLg2
    );

    ADC_EventBuffer_HG1: DoubleBuffer
    generic map(
        G_WIDTH => 21,
        G_DEPTH =>6
    )
    port map(
        RESET => RESET,
        WCLK => ADC_CLK,
        DIN => CoreDoutHg1,
        WADDR => CoreAddrHg1,
        WE => CoreWeHg1,
        WCOMP => CoreWcompHg1,
        DEC_WPTR => CoreDecWptrHg1,
        FULL => EventBufferFullHg1,
        RCLK => SITCP_CLK,
        DOUT => ADC_DOUT_HG1,
        RADDR => ADC_RADDR_HG1,
        RCOMP => ADC_RCOMP_HG1,
        EMPTY => ADC_EMPTY_HG1
    );

    ADC_EventBuffer_LG1: DoubleBuffer
    generic map(
        G_WIDTH => 21,
        G_DEPTH =>6
    )
    port map(
        RESET => RESET,
        WCLK => ADC_CLK,
        DIN => CoreDoutLg1,
        WADDR => CoreAddrLg1,
        WE => CoreWeLg1,
        WCOMP => CoreWcompLg1,
        DEC_WPTR => CoreDecWptrLg1,
        FULL => EventBufferFullLg1,
        RCLK => SITCP_CLK,
        DOUT => ADC_DOUT_LG1,
        RADDR => ADC_RADDR_LG1,
        RCOMP => ADC_RCOMP_LG1,
        EMPTY => ADC_EMPTY_LG1
    );

    ADC_EventBuffer_HG2: DoubleBuffer
    generic map(
        G_WIDTH => 21,
        G_DEPTH =>6
    )
    port map(
        RESET => RESET,
        WCLK => ADC_CLK,
        DIN => CoreDoutHg2,
        WADDR => CoreAddrHg2,
        WE => CoreWeHg2,
        WCOMP => CoreWcompHg2,
        DEC_WPTR => CoreDecWptrHg2,
        FULL => EventBufferFullHg2,
        RCLK => SITCP_CLK,
        DOUT => ADC_DOUT_HG2,
        RADDR => ADC_RADDR_HG2,
        RCOMP => ADC_RCOMP_HG2,
        EMPTY => ADC_EMPTY_HG2
    );

    ADC_EventBuffer_LG2: DoubleBuffer
    generic map(
        G_WIDTH => 21,
        G_DEPTH =>6
    )
    port map(
        RESET => RESET,
        WCLK => ADC_CLK,
        DIN => CoreDoutLg2,
        WADDR => CoreAddrLg2,
        WE => CoreWeLg2,
        WCOMP => CoreWcompLg2,
        DEC_WPTR => CoreDecWptrLg2,
        FULL => EventBufferFullLg2,
        RCLK => SITCP_CLK,
        DOUT => ADC_DOUT_LG2,
        RADDR => ADC_RADDR_LG2,
        RCOMP => ADC_RCOMP_LG2,
        EMPTY => ADC_EMPTY_LG2
    );

    RBCP_ACK <= RbcpAckCoreHg1 or RbcpAckCoreHg2 or RbcpAckCoreLg1 or RbcpAckCoreLg2;

end RTL;

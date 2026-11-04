--------------------------------------------------------------------------------
--! @file   ADC_Core.vhd
--! @brief  read ADC data from AD9220
--! @author Takehiro Shiozaki
--! @date   2013-11-11
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity ADC_Core is
    generic(
        G_IS_LAST_CHANNEL : std_logic := '0';
        G_IS_LOW_GAIN : std_logic := '0';
        G_PEDESTAL_SUPPRESSION_ADDR : std_logic_vector(31 downto 0) := (others => '0')
    );
    port (
        ADC_CLK : in  std_logic;
        RESET : in  std_logic;

        -- Control interface
        START : in std_logic;
        FAST_CLEAR : in std_logic;
        BUSY : out std_logic;

        -- AD9220 interface
        ADC_DATA : in std_logic_vector(11 downto 0);
        ADC_OTR : in std_logic;

        -- EventBuffer interface
        DOUT : out std_logic_vector(20 downto 0);
        ADDR : out std_logic_vector(5 downto 0);
        WE : out std_logic;
        FULL : in std_logic;
        WCOMP : out std_logic;
        DEC_WPTR : out std_logic;

        -- RBCP interface
        RBCP_CLK : in std_logic;
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic
    );
end ADC_Core;

architecture RTL of ADC_Core is

    component DualPortRam
    generic(
        G_WIDTH : integer;
        G_DEPTH : integer
    );
    port(
        WCLK : in  std_logic;
        DIN : in  std_logic_vector(G_WIDTH - 1 downto 0);
        WADDR : in  std_logic_vector(G_DEPTH - 1 downto 0);
        WE : in  std_logic;
        RCLK : in  std_logic;
        DOUT : out  std_logic_vector(G_WIDTH - 1 downto 0);
        RADDR : in  std_logic_vector(G_DEPTH - 1 downto 0)
    );
    end component;

    component RBCP_Receiver16bit
    generic (
        G_ADDR : std_logic_vector(31 downto 0);
        G_LEN : integer;
        G_ADDR_WIDTH : integer
    );
    port(
        CLK : in  std_logic;
        RESET : in  std_logic;
        RBCP_ACT : in  std_logic;
        RBCP_ADDR : in  std_logic_vector(31 downto 0);
        RBCP_WE : in  std_logic;
        RBCP_WD : in  std_logic_vector(7 downto 0);
        RBCP_ACK : out  std_logic;
        ADDR : out  std_logic_vector(G_ADDR_WIDTH - 1 downto 0);
        WE : out  std_logic;
        WD : out  std_logic_vector(15 downto 0)
    );
    end component;

    signal Channel : std_logic_vector(4 downto 0);
    signal ChannelCountUp : std_logic;
    signal ChannelCountClear : std_logic;
    signal DelayedChannel : std_logic_vector(4 downto 0);

    signal Address : std_logic_vector(5 downto 0);
    signal AddressCountUp : std_logic;
    signal AddressCountClear : std_logic;

    signal AdcData : std_logic_vector(12 downto 0);
    signal DelayedAdcData : std_logic_vector(12 downto 0);

    signal NotSuppressed : std_logic;
    signal WriteFooter : std_logic;
    signal WriteDataSize : std_logic;
    signal WriteEnableMask : std_logic;

    signal PedestalSuppressionRamDin : std_logic_vector(15 downto 0);
    signal PedestalSuppressionRamWaddr : std_logic_vector(4 downto 0);
    signal PedestalSuppressionRamWe : std_logic;

    signal PedestalThreshold : std_logic_vector(15 downto 0);

    signal int_WE : std_logic;

    type State is (IDLE, WAIT_ADC_DATA, COUNTUP_CHANNEL, WRITE_ADC_DATA,
                   WRITE_FOOTER, WRITE_DATA_SIZE, WAIT_FULL, WRITE_COMPLETE,
                   CLEAR);
    signal CurrentState, NextState : State;

begin

    RBCP_Receiver_0: RBCP_Receiver16bit
    generic map(
        G_ADDR => G_PEDESTAL_SUPPRESSION_ADDR,
        G_LEN => 64,
        G_ADDR_WIDTH => 5
    )
    port map (
        CLK => RBCP_CLK,
        RESET => RESET,
        RBCP_ACT => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE => RBCP_WE,
        RBCP_WD => RBCP_WD,
        RBCP_ACK => RBCP_ACK,
        ADDR => PedestalSuppressionRamWaddr,
        WE => PedestalSuppressionRamWe,
        WD => PedestalSuppressionRamDin
    );

    PedestalSuppressionRam: DualPortRam
    generic map (
        G_WIDTH => 16,
        G_DEPTH => 5
    )
    port map (
        WCLK => RBCP_CLK,
        DIN => PedestalSuppressionRamDin,
        WADDR => PedestalSuppressionRamWaddr,
        WE => PedestalSuppressionRamWe,
        RCLK => ADC_CLK,
        DOUT => PedestalThreshold,
        RADDR => Channel
    );

    process(ADC_CLK, RESET)
    begin
        if(RESET = '1') then
            CurrentState <= IDLE;
        elsif(ADC_CLK'event and ADC_CLK = '1') then
            CurrentState <= NextState;
        end if;
    end process;

    process(CurrentState, START, FAST_CLEAR, DelayedChannel, FULL)
    begin
        case(CurrentState) is
            when IDLE =>
                if(FAST_CLEAR = '1') then
                    NextState <= CLEAR;
                elsif(START = '1') then
                    NextState <= WAIT_ADC_DATA;
                else
                    NextState <= CurrentState;
                end if;
            when WAIT_ADC_DATA =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= COUNTUP_CHANNEL;
                end if;
            when COUNTUP_CHANNEL =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= WRITE_ADC_DATA;
                end if;
            when WRITE_ADC_DATA =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    if(DelayedChannel = 31) then
                        NextState <= WRITE_FOOTER;
                    else
                        NextState <= COUNTUP_CHANNEL;
                    end if;
                end if;
            when WRITE_FOOTER =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= WRITE_DATA_SIZE;
                end if;
            when WRITE_DATA_SIZE =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    if(FULL = '1') then
                        NextState <= WAIT_FULL;
                    else
                        NextState <= WRITE_COMPLETE;
                    end if;
                end if;
            when WAIT_FULL =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    if(FULL = '1') then
                        NextState <= CurrentState;
                    else
                        NextState <= WRITE_COMPLETE;
                    end if;
                end if;
            when WRITE_COMPLETE =>
                if(FAST_CLEAR = '1') then
                    NextState <= CLEAR;
                else
                    NextState <= IDLE;
                end if;
            when CLEAR =>
                NextState <= IDLE;
        end case;
    end process;

    process(ADC_CLK, RESET)
    begin
        if(RESET = '1') then
            Channel <= (others => '0');
        elsif(ADC_CLK'event and ADC_CLK = '1') then
            if(ChannelCountClear = '1') then
                Channel <= (others => '0');
            elsif(ChannelCountUp = '1') then
                Channel <= Channel + 1;
            end if;
        end if;
    end process;

    process(ADC_CLK, RESET)
    begin
        if(RESET = '1') then
            Address <= (others => '0');
        elsif(ADC_CLK'event and ADC_CLK = '1') then
            if(AddressCountClear = '1') then
                Address <= (others => '0');
            elsif(AddressCountUp = '1') then
                Address <= Address + 1;
            end if;
        end if;
    end process;

    AdcData <= ADC_OTR & ADC_DATA;

    process(ADC_CLK, RESET)
    begin
        if(RESET = '1') then
            DelayedAdcData <= (others => '0');
        elsif(ADC_CLK'event and ADC_CLK = '1') then
            DelayedAdcData <= AdcData;
        end if;
    end process;

    process(ADC_CLK, RESET)
    begin
        if(RESET = '1') then
            DelayedChannel <= (others => '0');
        elsif(ADC_CLK'event and ADC_CLK = '1') then
            DelayedChannel <= Channel;
        end if;
    end process;

    process(PedestalThreshold, DelayedAdcData)
    begin
        if((PedestalThreshold(15) or PedestalThreshold(14) or
            PedestalThreshold(13) or PedestalThreshold(12)) = '1') then
            NotSuppressed <= '1';
        else
            if(PedestalThreshold(11 downto 0) < DelayedAdcData(11 downto 0)) then
                NotSuppressed <= '1';
            else
                NotSuppressed <= '0';
            end if;
        end if;
    end process;

    int_WE <= (NotSuppressed or WriteFooter or WriteDataSize) and WriteEnableMask;
    WE <= int_WE;

    process(WriteFooter, WriteDataSize, DelayedChannel, DelayedAdcData, Address)
    begin
        if(WriteFooter = '1') then
            DOUT <= '1' & not G_IS_LOW_GAIN & not G_IS_LAST_CHANNEL & "000000000000000000";
        elsif(WriteDataSize = '1') then
            DOUT <= "000000000000000" & Address;
        else
            DOUT <= '0' & G_IS_LOW_GAIN & G_IS_LAST_CHANNEL & DelayedChannel & DelayedAdcData;
        end if;
    end process;

    WriteFooter <= '1' when(CurrentState = WRITE_FOOTER) else
                   '0';
    WriteDataSize <= '1' when(CurrentState = WRITE_DATA_SIZE) else
                     '0';

    with(CurrentState) select
    WriteEnableMask <= '1' when WRITE_ADC_DATA | WRITE_FOOTER | WRITE_DATA_SIZE,
                       '0' when others;

    WCOMP <= '1' when(CurrentState = WRITE_COMPLETE) else
             '0';

    ChannelCountClear <= '1' when(CurrentState = IDLE or CurrentState = CLEAR) else
                         '0';

    ChannelCountUp <= '1' when(CurrentState = COUNTUP_CHANNEL) else
                      '0';

    with(CurrentState) select
    AddressCountClear <= '1' when IDLE | CLEAR | WAIT_ADC_DATA,
                         '0' when others;

    AddressCountUp <= '1' when(int_WE = '1' and CurrentState = WRITE_ADC_DATA) else
                      '0';

    BUSY <= '0' when(CurrentState = IDLE or CurrentState = CLEAR) else
            '1';

    ADDR <= Address when(WriteDataSize = '0') else
            "111111";

    DEC_WPTR <= '1' when(CurrentState = CLEAR) else
                '0';

end RTL;


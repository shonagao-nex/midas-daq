--------------------------------------------------------------------------------
--! @file   MHTDC_Builder.vhd
--! @brief  Build 64ch MHTDC data
--! @author Takehiro Shiozaki
--! @date   2014-06-08
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library work;
use work.MHTDC_DataType.all;

entity MHTDC_Builder is
    generic(
        G_LT : std_logic := '0'
    );
    port(
        TDC_CLK : in std_logic;
        RESET : in std_logic;

        DIN : in MHTDC_CounterArray;
        RE : out std_logic_vector(63 downto 0);
        EMPTY : in std_logic_vector(63 downto 0);
        CLEAR : out std_logic_vector(63 downto 0);

        -- Commom Stop interface
        COMMON_STOP : in std_logic;
        COMMON_STOP_COUNT : in std_logic_vector(45 downto 0);
        FAST_CLEAR : in std_logic;

        -- Double buffer interface
        DOUT : out std_logic_vector(19 downto 0);
        WADDR : out std_logic_vector(10 downto 0);
        WE : out std_logic;
        FULL : in std_logic;
        WCOMP : out std_logic;
        DEC_WPTR : out std_logic;

        -- Control interface
        TIME_WINDOW : in std_logic_vector(11 downto 0);
        BUSY : out std_logic
    );
end MHTDC_Builder;

architecture RTL of MHTDC_Builder is
    signal SelectedDin : std_logic_vector(45 downto 0);
    signal CommonRe : std_logic;
    signal SelectedEmpty : std_logic;
    signal DelayedSelectedEmpty : std_logic;
    signal CommonClear : std_logic;

    signal Channel : std_logic_vector(5 downto 0);
    signal ChannelCountUp : std_logic;
    signal ChannelCountClear : std_logic;

    signal WriteAddress : std_logic_vector(10 downto 0);
    signal WriteAddressCountUp : std_logic;
    signal WriteAddressCountClear : std_logic;

    signal OffsettedDin : std_logic_vector(45 downto 0);
    signal InTimeWindow : std_logic;
    signal TimeDifference : std_logic_vector(11 downto 0);
    signal WeEnableData : std_logic;
    signal WeEnableFooter : std_logic;
    signal WeEnableDataSize : std_logic;
    signal int_WE : std_logic;

    signal DelayedWe : std_logic;
    signal DelayedDout : std_logic_vector(19 downto 0);
    signal DelayedWaddr : std_logic_vector(10 downto 0);
    signal DelayedWcomp : std_logic;

    signal IsFooter : std_logic;

    signal CommonStopCount : std_logic_vector(45 downto 0);
    signal TimeWindow : std_logic_vector(11 downto 0);

    type State is (IDLE, READ_DATA, WRITE_DATA, NEXT_CHANNEL,
                   WRITE_FOOTER, WRITE_DATA_SIZE, WAIT_FULL,
                   WRITE_COMPLETE, CLEAR_STATE);
    signal CurrentState, NextState : State;
begin

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(ChannelCountClear = '1') then
                Channel <= (others => '0');
            elsif(ChannelCountUp = '1') then
                Channel <= Channel + 1;
            end if;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(WriteAddressCountClear = '1') then
                WriteAddress <= (others => '0');
            elsif(WriteAddressCountUp = '1') then
                WriteAddress <= WriteAddress + 1;
            end if;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(COMMON_STOP = '1') then
                CommonStopCount <= COMMON_STOP_COUNT;
            end if;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(COMMON_STOP = '1') then
                TimeWindow <= TIME_WINDOW;
            end if;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            if(RESET = '1') then
                CurrentState <= IDLE;
            else
                CurrentState <= NextState;
            end if;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            DelayedSelectedEmpty <= SelectedEmpty;
            DelayedWe <= int_WE;

            if(CurrentState = WRITE_DATA_SIZE) then
                DelayedDout <= conv_std_logic_vector(0, 9) & WriteAddress;
                DelayedWaddr <= (others => '1');
            elsif(CurrentState = WRITE_FOOTER) then
                DelayedDout <= IsFooter & "111111" & not G_LT & X"fff";
                DelayedWaddr <= WriteAddress;
            else
                DelayedDout <= IsFooter & Channel & G_LT & TimeDifference;
                DelayedWaddr <= WriteAddress;
            end if;

            if(CurrentState = WRITE_COMPLETE) then
                DelayedWcomp <= '1';
            else
                DelayedWcomp <= '0';
            end if;
        end if;
    end process;

    process(CurrentState, COMMON_STOP, FAST_CLEAR, InTimeWindow, SelectedEmpty,
            Channel, FULL)
    begin
        case CurrentState is
        when IDLE =>
            if(FAST_CLEAR = '1') then
                NextState <= CLEAR_STATE;
            else
                if(COMMON_STOP = '1') then
                    NextState <= READ_DATA;
                else
                    NextState <= CurrentState;
                end if;
            end if;
        when READ_DATA =>
            if(FAST_CLEAR = '1') then
                NextState <= IDLE;
            else
                NextState <= WRITE_DATA;
            end if;
        when WRITE_DATA =>
            if(FAST_CLEAR = '1') then
                NextState <= IDLE;
            else
                if(InTimeWindow = '0' or SelectedEmpty = '1') then
                    if(Channel = 63) then
                        NextState <= WRITE_FOOTER;
                    else
                        NextState <= NEXT_CHANNEL;
                    end if;
                else
                    NextState <= READ_DATA;
                end if;
            end if;
        when NEXT_CHANNEL =>
            if(FAST_CLEAR = '1') then
                NextState <= IDLE;
            else
                NextState <= READ_DATA;
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
                NextState <= CLEAR_STATE;
            else
                NextState <= IDLE;
            end if;
        when CLEAR_STATE =>
            NextState <= IDLE;
        end case;
    end process;

    OffsettedDin <= CommonStopCount - SelectedDin;
    InTimeWindow <= '1' when(OffsettedDin < TimeWindow) else
                    '0';
    TimeDifference <= OffsettedDin(11 downto 0);
    DOUT <= DelayedDout;
    WADDR <= DelayedWaddr;
    int_WE <= WeEnableData or WeEnableFooter or WeEnableDataSize;
    WE <= DelayedWe;

    WriteAddressCountUp <= int_WE when(CurrentState = WRITE_DATA) else
                           '0';
    WriteAddressCountClear <= '1' when(CurrentState = IDLE or
                                       CurrentState = CLEAR_STATE) else
                              '0';

    ChannelCountUp <= '1' when(CurrentState = NEXT_CHANNEL) else
                      '0';
    ChannelCountClear <= '1' when(CurrentState = IDLE or
                                  CurrentState = CLEAR_STATE) else
                         '0';

    BUSY <= '0' when(CurrentState = IDLE or CurrentState = CLEAR_STATE) else
            '1';
    CommonRe <= '1' when(CurrentState = READ_DATA) else
                '0';
    CommonClear <= '1' when(CurrentState = NEXT_CHANNEL) else
                   '0';
    IsFooter <= '1' when(CurrentState = WRITE_FOOTER) else
                '0';
    WeEnableData <= '1' when(CurrentState = WRITE_DATA and InTimeWindow = '1' and
                             DelayedSelectedEmpty = '0') else
                    '0';
    WeEnableFooter <= '1' when(CurrentState = WRITE_FOOTER) else
                      '0';
    WeEnableDataSize <= '1' when(CurrentState = WRITE_DATA_SIZE) else
                        '0';


    WCOMP <= DelayedWcomp;

    SelectedDin <= DIN(conv_integer(Channel));

    RE_GENERATE: for i in 0 to 63 generate
        RE(i) <= CommonRe when(Channel = i) else
                 '0';
    end generate RE_GENERATE;

    SelectedEmpty <= EMPTY(conv_integer(Channel));

    CLEAR_GENERATE: for i in 0 to 63 generate
        CLEAR(i) <= CommonClear when(Channel = i) else
                  '0';
    end generate CLEAR_GENERATE;

    DEC_WPTR <= '1' when(CurrentState = CLEAR_STATE) else
                '0';

end RTL;

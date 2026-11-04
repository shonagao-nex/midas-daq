--------------------------------------------------------------------------------
--! @file   ADC_Controller.vhd
--! @brief  Controll ADC_Core, EASIROC, and AD9220
--! @author Takehiro Shiozaki
--! @date   2013-11-11
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity ADC_Controller is
    port (
        ADC_CLK : in  std_logic;
        AD9220_CLK : in std_logic;
        RESET : in  std_logic;

        -- Control interface
        TRIGGER : in  std_logic;
        FAST_CLEAR : in std_logic;
        BUSY : out  std_logic;
        AD9220_CLK_ENABLE : out std_logic;

        -- ADC Core interface
        CORE_START : out std_logic;
        CORE_BUSY0 : in std_logic;
        CORE_BUSY1 : in std_logic;
        CORE_BUSY2 : in std_logic;
        CORE_BUSY3 : in std_logic;

        -- EASIROC interface
        CLK_READ : out std_logic;
        RSTB_READ : out std_logic;
        SRIN_READ : out std_logic
    );
end ADC_Controller;

architecture RTL of ADC_Controller is

    component SynchEdgeDetector
        port(
            CLK : in std_logic;
            RESET : in std_logic;
            DIN : in std_logic;
            DOUT : out std_logic
        );
    end component;

    signal SynchTrigger : std_logic;

    signal Channel : std_logic_vector(4 downto 0);
    signal ChannelCountClear : std_logic;
    signal ChannelCountUp : std_logic;

    signal InternalBusy : std_logic;

    type State is (IDLE, SRIN_HIGH_0, SRIN_HIGH_1, CLK_LOW, CLK_HIGH);
    signal CurrentState, NextState : State;

begin

    SynchEdgeDetector_0: SynchEdgeDetector
    port map(
        CLK => AD9220_CLK,
        RESET => RESET,
        DIN => TRIGGER,
        DOUT => SynchTrigger
    );

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
            CurrentState <= IDLE;
        elsif(ADC_CLK'event and ADC_CLK = '1') then
            CurrentState <= NextState;
        end if;
    end process;

    process(CurrentState, FAST_CLEAR, SynchTrigger, Channel)
    begin
        case(CurrentState) is
            when IDLE =>
                if(SynchTrigger = '1') then
                    NextState <= SRIN_HIGH_0;
                else
                    NextState <= CurrentState;
                end if;
            when SRIN_HIGH_0 =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= SRIN_HIGH_1;
                end if;
            when SRIN_HIGH_1 =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= CLK_LOW;
                end if;
            when CLK_LOW =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    if(Channel = 0) then
                        NextState <= IDLE;
                    else
                        NextState <= CLK_HIGH;
                    end if;
                end if;
            when CLK_HIGH =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= CLK_LOW;
                end if;
        end case;
    end process;

    CORE_START <= '1' when(CurrentState = CLK_LOW and Channel = 4) else
                  '0';

    with(CurrentState) select
        CLK_READ <= '1' when SRIN_HIGH_1 | CLK_HIGH,
                    '0' when others;

    with(CurrentState) select
        RSTB_READ <= '0' when IDLE,
                     '1' when others;

    with(CurrentState) select
        SRIN_READ <= '1' when SRIN_HIGH_0 | SRIN_HIGH_1,
                     '0' when others;

    ChannelCountClear <= '1' when(CurrentState = IDLE) else
                         '0';

    with(CurrentState) select
        ChannelCountUp <= '1' when SRIN_HIGH_1 | CLK_HIGH,
                          '0' when others;

    InternalBusy <= '0' when(CurrentState = IDLE) else
                    '1';

    BUSY <= InternalBusy or CORE_BUSY0 or CORE_BUSY1 or CORE_BUSY2 or CORE_BUSY3;

    AD9220_CLK_ENABLE <= InternalBusy or CORE_BUSY0 or CORE_BUSY1 or CORE_BUSY2 or CORE_BUSY3;
end RTL;


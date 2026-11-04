--------------------------------------------------------------------------------
--! @file   TriggerManager.vhd
--! @brief  Manage and distribute trigger
--! @author Naruhiro Chikuma
--! @date   2015-09-06
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity TriggerManager is
    port(
        SITCP_CLK : in  std_logic;
        ADC_CLK : in std_logic;
        AD9220_CLK : in std_logic;
        TDC_CLK : in std_logic;
        SCALER_CLK : in std_logic;
        FAST_CLK : in std_logic;
        RESET : in  std_logic;

        -- Trigger
        HOLD : in std_logic;
        L1_TRIGGER : in std_logic;
        L2_TRIGGER : in std_logic;
        FAST_CLEAR : in std_logic;
        BUSY : out std_logic;

        -- Sender interface
        TRANSMIT_START : out std_logic;

        GATHERER_BUSY : in std_logic;

        -- Control
        IS_DAQ_MODE : in std_logic;
        TCP_OPEN_ACK : in std_logic;

        -- ADC interface
        ADC_TRIGGER : out std_logic;
        ADC_FAST_CLEAR : out std_logic;
        ADC_BUSY : in std_logic;

        -- TDC intreface
        COMMON_STOP : out std_logic;
        TDC_FAST_CLEAR : out std_logic;
        TDC_BUSY : in std_logic;

        -- Scaler interface
        SCALER_TRIGGER : out std_logic;
        SCALER_FAST_CLEAR : out std_logic;
        SCALER_BUSY : in std_logic;

        -- Hold
        HOLD_OUT1_N : out std_logic;
        HOLD_OUT2_N : out std_logic
    );
end TriggerManager;

architecture RTL of TriggerManager is

    component Synchronizer is
        port(
            CLK : in  std_logic;
            RESET : in  std_logic;
            DIN : in std_logic;
            DOUT : out std_logic
        );
    end component;

    component SynchEdgeDetector
        port(
            CLK : in std_logic;
            RESET : in std_logic;
            DIN : in std_logic;
            DOUT : out std_logic
        );
    end component;

    component InterclockTrigger is
        port(
            CLK_IN : in std_logic;
            CLK_OUT : in std_logic;
            RESET : in std_logic;
            TRIGGER_IN : in std_logic;
            TRIGGER_OUT : out std_logic
        );
    end component;

    component HoldExpander is
        port(
            FAST_CLK : in std_logic;
            RESET : in  std_logic;

            HOLD_IN : in std_logic;
            HOLD_OUT1_N : out std_logic;
            HOLD_OUT2_N : out std_logic;

            EXTERNAL_RESET_HOLD : in std_logic;
            IS_EXTERNAL_RESET_HOLD : in std_logic
        );
    end component;

    component BusyManager is
        port(
            FAST_CLK : in std_logic;
            RESET : in  std_logic;

            HOLD : in std_logic;
            RESET_BUSY : in std_logic;
            BUSY : out std_logic
        );
    end component;

    signal int_Busy : std_logic;

    signal DelayedAdcBusy : std_logic;
    signal DelayedTdcBusy : std_logic;
    signal DelayedScalerBusy : std_logic;
    signal DelayedIsDaqMode : std_logic;
    signal DelayedIsDaqMode_N : std_logic;
    signal IsDaqModeNEdge : std_logic;

    signal AdcTdcScalerBusy : std_logic;
    signal SynchAdcTdcScalerBusy : std_logic;
    signal SynchGathererBusy : std_logic;

    signal MaskedHold : std_logic;
    signal MaskedL1 : std_logic;
    signal MaskedL2 : std_logic;
    signal MaskedFastClear : std_logic;

    signal HoldEdge : std_logic;
    signal L1Edge : std_logic;
    signal L2Edge : std_logic;
    signal FastClearEdge : std_logic;

    signal CommonStopMask : std_logic;

    signal ResetHoldBusy : std_logic;

    signal SendAdcTrigger : std_logic;
    signal SendTransmitStart : std_logic;
    signal ResetBusy : std_logic;

    signal SendFastClear : std_logic;

    type State is (IDLE, SEND_ADC_TRIGGER, HOLD_RECEIVED, L1_RECEIVED,
                   WAIT_GATHERER_BUSY, CLEAR_STATE, SEND_TRANSMIT_START,
                   WAIT_ADC_TDC_BUSY, RESET_BUSY);
    signal CurrentState, NextState : State;

begin

    MaskedHold <= HOLD and (not int_BUSY) and IS_DAQ_MODE and TCP_OPEN_ACK;
    MaskedL1 <= L1_TRIGGER and IS_DAQ_MODE and TCP_OPEN_ACK;
    MaskedL2 <= L2_TRIGGER and IS_DAQ_MODE and TCP_OPEN_ACK;
    MaskedFastClear <= FAST_CLEAR and IS_DAQ_MODE and TCP_OPEN_ACK;

    SynchEdgeDetector_HOLD: SynchEdgeDetector
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => MaskedHold,
        DOUT => HoldEdge
    );

    SynchEdgeDetector_L1: SynchEdgeDetector
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => MaskedL1,
        DOUT => L1Edge
    );

    SynchEdgeDetector_L2: SynchEdgeDetector
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => MaskedL2,
        DOUT => L2Edge
    );

    SynchEdgeDetector_FAST_CLEAR: SynchEdgeDetector
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => MaskedFastClear,
        Dout => FastClearEdge
    );

    process(FAST_CLK)
    begin
        if(FAST_CLK'event and FAST_CLK = '1') then
            if(RESET = '1') then
                CurrentState <= IDLE;
            else
                CurrentState <= NextState;
            end if;
        end if;
    end process;

    Synchronizer_GathererBusy: Synchronizer
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => GATHERER_BUSY,
        DOUT => SynchGathererBusy
    );

    process(CurrentState, HoldEdge, L1Edge, L2Edge, FastClearEdge, int_BUSY,
            SynchGathererBusy, SynchAdcTdcScalerBusy)
    begin
        case CurrentState is
            when IDLE =>
                if(HoldEdge = '1') then
                    NextState <= SEND_ADC_TRIGGER;
                else
                    NextState <= CurrentState;
                end if;
            when SEND_ADC_TRIGGER =>
                NextState <= HOLD_RECEIVED;
            when HOLD_RECEIVED =>
                if(L1Edge = '1') then
                    NextState <= L1_RECEIVED;
                else
                    NextState <= CurrentState;
                end if;
            when L1_RECEIVED =>
                if(FastClearEdge = '1') then
                    NextState <= CLEAR_STATE;
                elsif(L2Edge = '1') then
                    NextState <= WAIT_GATHERER_BUSY;
                else
                    NextState <= CurrentState;
                end if;
            when CLEAR_STATE =>
                NextState <= WAIT_ADC_TDC_BUSY;
            when WAIT_GATHERER_BUSY =>
                if(SynchGathererBusy = '1') then
                    NextState <= CurrentState;
                else
                    NextState <= SEND_TRANSMIT_START;
                end if;
            when SEND_TRANSMIT_START =>
                NextState <= WAIT_ADC_TDC_BUSY;
            when WAIT_ADC_TDC_BUSY =>
                if(SynchAdcTdcScalerBusy = '0') then
                    NextState <= RESET_BUSY;
                else
                    NextState <= CurrentState;
                end if;
            when RESET_BUSY =>
                NextState <= IDLE;
        end case;
    end process;

    SendAdcTrigger <= '1' when(CurrentState = SEND_ADC_TRIGGER) else
                      '0';
    SendTransmitStart <= '1' when(CurrentState = SEND_TRANSMIT_START) else
                         '0';
    SendFastClear <= '1' when(CurrentState = CLEAR_STATE) else
                     '0';

    ResetBusy <= '1' when(CurrentState = RESET_BUSY) else
                 '0';

    process(FAST_CLK)
    begin
        if(FAST_CLK'event and FAST_CLK = '1') then
            if(CurrentState = HOLD_RECEIVED or CurrentState = L1_RECEIVED) then
                CommonStopMask <= '1';
            else
                CommonStopMask <= '0';
            end if;
        end if;
    end process;

    COMMON_STOP <= L1_TRIGGER and CommonStopMask;

    InterclockTrigger_AdcTrigger: InterclockTrigger
    port map(
        CLK_IN => FAST_CLK,
        CLK_OUT => AD9220_CLK,
        RESET => RESET,
        TRIGGER_IN => SendAdcTrigger,
        TRIGGER_OUT => ADC_TRIGGER
    );

    InterclockTrigger_ScalerTrigger: InterclockTrigger
    port map(
        CLK_IN => FAST_CLK,
        CLK_OUT => SCALER_CLK,
        RESET => RESET,
        TRIGGER_IN => L1Edge,
        TRIGGER_OUT => SCALER_TRIGGER
    );

    InterclockTrigger_TransmitStart: InterclockTrigger
    port map(
        CLK_IN => FAST_CLK,
        CLK_OUT => SITCP_CLK,
        RESET => RESET,
        TRIGGER_IN => SendTransmitStart,
        TRIGGER_OUT => TRANSMIT_START
    );

    InterclockTrigger_AdcFastClear: InterclockTrigger
    port map(
        CLK_IN => FAST_CLK,
        CLK_OUT => ADC_CLK,
        RESET => RESET,
        TRIGGER_IN => SendFastClear,
        TRIGGER_OUT => ADC_FAST_CLEAR
    );

    InterclockTrigger_TdcFastClear: InterclockTrigger
    port map(
        CLK_IN => FAST_CLK,
        CLK_OUT => TDC_CLK,
        RESET => RESET,
        TRIGGER_IN => SendFastClear,
        TRIGGER_OUT => TDC_FAST_CLEAR
    );

    InterclockTrigger_ScalerFastClear: InterclockTrigger
    port map(
        CLK_IN => FAST_CLK,
        CLK_OUT => SCALER_CLK,
        RESET => RESET,
        TRIGGER_IN => SendFastClear,
        TRIGGER_OUT => SCALER_FAST_CLEAR
    );


    process(SITCP_CLK)
    begin
        if(SITCP_CLK'event and SITCP_CLK = '1') then
            DelayedIsDaqMode <= IS_DAQ_MODE;
        end if;
    end process;

    DelayedIsDaqMode_N <= not DelayedIsDaqMode;
    SynchEdgeDetector_IsDaqMode: SynchEdgeDetector
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => DelayedIsDaqMode_N,
        DOUT => IsDaqModeNEdge
    );

    ResetHoldBusy <= RESET or IsDaqModeNEdge;
    HoldExpander_0: HoldExpander
    port map(
        FAST_CLK => FAST_CLK,
        RESET => ResetHoldBusy,
        HOLD_IN => HOLD,
        HOLD_OUT1_N => HOLD_OUT1_N,
        HOLD_OUT2_N => HOLD_OUT2_N,
        EXTERNAL_RESET_HOLD => ResetBusy,
        IS_EXTERNAL_RESET_HOLD => IS_DAQ_MODE
    );

    BusyManager_0: BusyManager
    port map(
        FAST_CLK => FAST_CLK,
        RESET => ResetHoldBusy,
        HOLD => MaskedHold,
        RESET_BUSY => ResetBusy,
        BUSY => int_BUSY
    );

    BUSY <= int_BUSY;

    process(AD9220_CLK)
    begin
        if(AD9220_CLK'event and AD9220_CLK = '1') then
            DelayedAdcBusy <= ADC_BUSY;
        end if;
    end process;

    process(TDC_CLK)
    begin
        if(TDC_CLK'event and TDC_CLK = '1') then
            DelayedTdcBusy <= TDC_BUSY;
        end if;
    end process;

    process(SCALER_CLK)
    begin
        if(SCALER_CLK'event and SCALER_CLK = '1') then
            DelayedScalerBusy <= SCALER_BUSY;
        end if;
    end process;

    AdcTdcScalerBusy <= DelayedAdcBusy or DelayedTdcBusy or DelayedScalerBusy;

    Synchronizer_AdcTdcBusy: Synchronizer
    port map(
        CLK => FAST_CLK,
        RESET => RESET,
        DIN => AdcTdcScalerBusy,
        DOUT => SynchAdcTdcScalerBusy
    );

end RTL;

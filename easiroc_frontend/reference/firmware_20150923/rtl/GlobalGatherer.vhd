--------------------------------------------------------------------------------
--! @file   Gatherer.vhd
--! @brief  Gather ADC & TDC Data into FIFO
--! @author Naruhiro Chikuma
--! @date   2015-09-06
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

entity GlobalGatherer is
    port(
        CLK : in  std_logic;
        RESET : in  std_logic;

        -- ADC0
        ADC0_DIN : in  std_logic_vector (20 downto 0);
        ADC0_RADDR : out  std_logic_vector (5 downto 0);
        ADC0_RCOMP : out  std_logic;
        ADC0_EMPTY : in  std_logic;

        -- ADC1
        ADC1_DIN : in  std_logic_vector (20 downto 0);
        ADC1_RADDR : out  std_logic_vector (5 downto 0);
        ADC1_RCOMP : out  std_logic;
        ADC1_EMPTY : in  std_logic;

        -- ADC2
        ADC2_DIN : in  std_logic_vector (20 downto 0);
        ADC2_RADDR : out  std_logic_vector (5 downto 0);
        ADC2_RCOMP : out  std_logic;
        ADC2_EMPTY : in  std_logic;

        -- ADC3
        ADC3_DIN : in  std_logic_vector (20 downto 0);
        ADC3_RADDR : out  std_logic_vector (5 downto 0);
        ADC3_RCOMP : out  std_logic;
        ADC3_EMPTY : in  std_logic;

        -- TDC (Leading)
        TDC_DIN_L : in  std_logic_vector (19 downto 0);
        TDC_RADDR_L : out  std_logic_vector (10 downto 0);
        TDC_RCOMP_L : out  std_logic;
        TDC_EMPTY_L : in  std_logic;

        -- TDC (Trailing)
        TDC_DIN_T : in  std_logic_vector (19 downto 0);
        TDC_RADDR_T : out  std_logic_vector (10 downto 0);
        TDC_RCOMP_T : out  std_logic;
        TDC_EMPTY_T : in  std_logic;

        -- Scaler
        SCALER_DIN : in std_logic_vector(20 downto 0);
        SCALER_RADDR : out std_logic_vector(6 downto 0);
        SCALER_RCOMP : out std_logic;
        SCALER_EMPTY : in std_logic;

        -- FIFO
        DOUT : out std_logic_vector(31 downto 0);
        WE : out std_logic;
        FULL : in std_logic;

        -- Control
        SEND_ADC : in  std_logic;
        SEND_TDC : in  std_logic;
        SEND_SCALER : in std_logic;
        TRIGGER : in  std_logic;

        BUSY : out  std_logic
    );
end GlobalGatherer;

architecture RTL of GlobalGatherer is

    component ADC_Gatherer
    port(
        CLK : in std_logic;
        RESET : in std_logic;
        DIN0 : in std_logic_vector(20 downto 0);
        EMPTY0 : in std_logic;
        DIN1 : in std_logic_vector(20 downto 0);
        EMPTY1 : in std_logic;
        DIN2 : in std_logic_vector(20 downto 0);
        EMPTY2 : in std_logic;
        DIN3 : in std_logic_vector(20 downto 0);
        EMPTY3 : in std_logic;
        FULL : in std_logic;
        START : in std_logic;
        RADDR0 : out std_logic_vector(5 downto 0);
        RCOMP0 : out std_logic;
        RADDR1 : out std_logic_vector(5 downto 0);
        RCOMP1 : out std_logic;
        RADDR2 : out std_logic_vector(5 downto 0);
        RCOMP2 : out std_logic;
        RADDR3 : out std_logic_vector(5 downto 0);
        RCOMP3 : out std_logic;
        DOUT : out std_logic_vector(20 downto 0);
        WE : out std_logic;
        BUSY : out std_logic
        );
    end component;

    component TDC_Gatherer is
        port(
            CLK : in std_logic;
            RESET : in  std_logic;

            -- TDC (Leading)
            DIN_L : in  std_logic_vector (19 downto 0);
            RADDR_L : out  std_logic_vector (10 downto 0);
            RCOMP_L : out  std_logic;
            EMPTY_L : in  std_logic;

            -- TDC (Trailing)
            DIN_T : in  std_logic_vector (19 downto 0);
            RADDR_T : out  std_logic_vector (10 downto 0);
            RCOMP_T : out  std_logic;
            EMPTY_T : in  std_logic;

            -- FIFO
            DOUT : out std_logic_vector(19 downto 0);
            WE : out std_logic;
            FULL : in std_logic;

            -- Control
            START : in std_logic;
            BUSY : out std_logic
            );
    end component;

    component ScalerGatherer is
        port (
            SITCP_CLK : in  std_logic;
            RESET : in  std_logic;

            -- Scaler
            DIN : in  std_logic_vector (20 downto 0);
            RADDR : out  std_logic_vector (6 downto 0);
            RCOMP : out  std_logic;
            EMPTY : in  std_logic;

            -- FIFO
            DOUT : out std_logic_vector(20 downto 0);
            WE : out std_logic;
            FULL : in std_logic;

            -- Control
            START : in std_logic;
            BUSY : out std_logic
        );
    end component;

    -- ADC
    signal AdcGathererRaddr0 : std_logic_vector(5 downto 0);
    signal AdcGathererRaddr1 : std_logic_vector(5 downto 0);
    signal AdcGathererRaddr2 : std_logic_vector(5 downto 0);
    signal AdcGathererRaddr3 : std_logic_vector(5 downto 0);
    signal AdcGathererRcomp0 : std_logic;
    signal AdcGathererRcomp1 : std_logic;
    signal AdcGathererRcomp2 : std_logic;
    signal AdcGathererRcomp3 : std_logic;
    signal AdcGathererWe : std_logic;
    signal AdcGathererDout : std_logic_vector(20 downto 0);
    signal AdcGathererStart : std_logic;
    signal AdcGathererBusy : std_logic;

    -- TDC
    signal TdcGathererRaddrLeading : std_logic_vector(10 downto 0);
    signal TdcGathererRaddrTrailing : std_logic_vector(10 downto 0);
    signal TdcGathererRcompLeading : std_logic;
    signal TdcGathererRcompTrailing : std_logic;
    signal TdcGathererWe : std_logic;
    signal TdcGathererDout : std_logic_vector(19 downto 0);
    signal TdcGathererStart : std_logic;
    signal TdcGathererBusy : std_logic;

    -- Scaler
    signal ScalerGathererRaddr : std_logic_vector(6 downto 0);
    signal ScalerGathererRcomp : std_logic;
    signal ScalerGathererWe : std_logic;
    signal ScalerGathererDout : std_logic_vector(20 downto 0);
    signal ScalerGathererStart : std_logic;
    signal ScalerGathererBusy : std_logic;

    -- DOUT, WE
    signal UnencodedDout : std_logic_vector(27 downto 0);
    signal AdcTdcScalerSel : std_logic_vector(1 downto 0);

    -- Header
    signal ReadDataSize : std_logic;
    signal WriteHeader : std_logic;
    signal Header : std_logic_vector(15 downto 0);
    signal HeaderEnable : std_logic;
    signal Adc0DataSize : std_logic_vector(11 downto 0);
    signal Adc1DataSize : std_logic_vector(11 downto 0);
    signal Adc2DataSize : std_logic_vector(11 downto 0);
    signal Adc3DataSize : std_logic_vector(11 downto 0);
    signal AdcDataSizeSum : std_logic_vector(11 downto 0);
    signal TdcLeadingDataSize : std_logic_vector(11 downto 0);
    signal TdcTrailingDataSize : std_logic_vector(11 downto 0);
    signal TdcDataSizeSum : std_logic_vector(11 downto 0);
    signal ScalerDataSize : std_logic_vector(11 downto 0);
    signal DataSize : std_logic_vector(11 downto 0);
    signal SpillNumber : std_logic;
    signal EventNumber : std_logic_vector(2 downto 0);

    -- Control
    signal SendAdc : std_logic;
    signal SendTdc : std_logic;
    signal SendScaler : std_logic;
    signal AdcReady : std_logic;
    signal TdcReady : std_logic;
    signal ScalerReady : std_logic;
    signal Ready : std_logic;

    -- FSM
    type State is (IDLE, WAIT_ADC_TDC_SCALER_READY, READ_DATA_SIZE,
                   STORE_DATA_SIZE, WAIT_FULL, WRITE_DATA_SIZE,
                   START_ADC_GATHERER, WAIT_ADC_GATHERER, ADC_READ_COMPLETE,
                   START_TDC_GATHERER, WAIT_TDC_GATHERER, TDC_READ_COMPLETE,
                   START_SCALER_GATHERER, WAIT_SCALER_GATHERER, SCALER_READ_COMPLETE
                  );
    signal CurrentState : State;
    signal NextState : State;

begin
    ADC_Gatherer_0: ADC_Gatherer
    port map(
        CLK => CLK,
        RESET => RESET,
        DIN0 => ADC0_DIN,
        RADDR0 => AdcGathererRaddr0,
        RCOMP0 => AdcGathererRcomp0,
        EMPTY0 => ADC0_EMPTY,
        DIN1 => ADC1_DIN,
        RADDR1 => AdcGathererRaddr1,
        RCOMP1 => AdcGathererRcomp1,
        EMPTY1 => ADC1_EMPTY,
        DIN2 => ADC2_DIN,
        RADDR2 => AdcGathererRaddr2,
        RCOMP2 => AdcGathererRcomp2,
        EMPTY2 => ADC2_EMPTY,
        DIN3 => ADC3_DIN,
        RADDR3 => AdcGathererRaddr3,
        RCOMP3 => AdcGathererRcomp3,
        EMPTY3 => ADC3_EMPTY,
        DOUT => AdcGathererDout,
        WE => AdcGathererWe,
        FULL => FULL,
        START => AdcGathererStart,
        BUSY => AdcGathererBusy
    );

    TDC_Gatherer_0: TDC_Gatherer
    port map(
        CLK => CLK,
        RESET => RESET,
        DIN_L => TDC_DIN_L,
        RADDR_L => TdcGathererRaddrLeading,
        RCOMP_L => TdcGathererRcompLeading,
        EMPTY_L => TDC_EMPTY_L,
        DIN_T => TDC_DIN_T,
        RADDR_T => TdcGathererRaddrTrailing,
        RCOMP_T => TdcGathererRcompTrailing,
        EMPTY_T => TDC_EMPTY_T,
        DOUT => TdcGathererDout,
        WE => TdcGathererWe,
        FULL => FULL,
        START => TdcGathererStart,
        BUSY => TdcGathererBusy
    );

    ScalerGatherer_0: ScalerGatherer
    port map(
        SITCP_CLK => CLK,
        RESET => RESET,
        DIN => SCALER_DIN,
        RADDR => ScalerGathererRaddr,
        RCOMP => ScalerGathererRcomp,
        EMPTY => SCALER_EMPTY,
        DOUT => ScalerGathererDout,
        WE => ScalerGathererWe,
        FULL => FULL,
        START => ScalerGathererStart,
        BUSY => ScalerGathererBusy
    );

    process(AdcGathererDout, TdcGathererDout, ScalerGathererDout,
            AdcTdcScalerSel, WriteHeader, Header)
    begin
        if(WriteHeader = '1') then
            UnencodedDout <= X"FFF" & Header;
        else
            if(AdcTdcScalerSel = "00") then
                UnencodedDout <= "00000" & AdcTdcScalerSel & AdcGathererDout;
            elsif(AdcTdcScalerSel = "01") then
                UnencodedDout <= "00000" & AdcTdcScalerSel & "0" & TdcGathererDout;
            else
                UnencodedDout <= "00000" & AdcTdcScalerSel & ScalerGathererDout;
            end if;
        end if;
    end process;

    DOUT <= '1' & UnencodedDout(27 downto 21) &
            '0' & UnencodedDout(20 downto 14) &
            '0' & UnencodedDout(13 downto  7) &
            '0' & UnencodedDout( 6 downto  0);

    process(AdcGathererWe, TdcGathererWe, ScalerGathererWe,
            AdcTdcScalerSel, WriteHeader)
    begin
        if(WriteHeader = '1') then
            WE <= '1';
        else
            if(AdcTdcScalerSel = "00") then
                WE <= AdcGathererWe;
            elsif(AdcTdcScalerSel = "01") then
                WE <= TdcGathererWe;
            else
                WE <= ScalerGathererWe;
            end if;
        end if;
    end process;

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            SpillNumber <= '0';
            EventNumber <= (others => '0');
            SendAdc <= '0';
            SendTdc <= '0';
            SendScaler <= '0';
        elsif(CLK'event and CLK = '1') then
            if(TRIGGER = '1') then
            	SpillNumber <= '1';
            	EventNumber <= (others => '1');
                SendAdc <= SEND_ADC;
                SendTdc <= SEND_TDC;
                SendScaler <= SEND_SCALER;
            end if;
        end if;
    end process;

    Adc0DataSize <= ADC0_DIN(11 downto 0);
    Adc1DataSize <= ADC1_DIN(11 downto 0);
    Adc2DataSize <= ADC2_DIN(11 downto 0);
    Adc3DataSize <= ADC3_DIN(11 downto 0);
    AdcDataSizeSum <= Adc0DataSize + Adc1DataSize +
                      Adc2DataSize + Adc3DataSize when(SendAdc = '1') else
                      (others => '0');
    TdcLeadingDataSize <= TDC_DIN_L(11 downto 0);
    TdcTrailingDataSize <= TDC_DIN_T(11 downto 0);
    TdcDataSizeSum <= TdcLeadingDataSize +
                      TdcTrailingDataSize when(SendTdc = '1') else
                      (others => '0');
    ScalerDataSize <= conv_std_logic_vector(69, 12) when(SendScaler = '1') else
                      (others => '0');
    DataSize <= AdcDataSizeSum + TdcDataSizeSum + ScalerDataSize;

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            Header <= (others => '0');
        elsif(CLk'event and CLK = '1') then
            if(HeaderEnable = '1') then
                Header <= SpillNumber & EventNumber & DataSize;
            end if;
        end if;
    end process;

    process(ReadDataSize, AdcGathererRaddr0, AdcGathererRaddr1,
            AdcGathererRaddr2, AdcGathererRaddr3,
            TdcGathererRaddrLeading, TdcGathererRaddrTrailing,
            ScalerGathererRaddr)
    begin
        if(ReadDataSize = '1') then
            ADC0_RADDR <= (others => '1');
            ADC1_RADDR <= (others => '1');
            ADC2_RADDR <= (others => '1');
            ADC3_RADDR <= (others => '1');
            TDC_RADDR_L <= (others => '1');
            TDC_RADDR_T <= (others => '1');
            SCALER_RADDR <= (others => '1');
        else
            ADC0_RADDR <= AdcGathererRaddr0;
            ADC1_RADDR <= AdcGathererRaddr1;
            ADC2_RADDR <= AdcGathererRaddr2;
            ADC3_RADDR <= AdcGathererRaddr3;
            TDC_RADDR_L <= TdcGathererRaddrLeading;
            TDC_RADDR_T <= TdcGathererRaddrTrailing;
            SCALER_RADDR <= ScalerGathererRaddr;
        end if;
    end process;

    AdcReady <= not(ADC0_EMPTY or ADC1_EMPTY or ADC2_EMPTY or ADC3_EMPTY);
    TdcReady <= not(TDC_EMPTY_L or TDC_EMPTY_T);
    ScalerReady <= not SCALER_EMPTY;
    Ready <= AdcReady and TdcReady and ScalerReady;

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            CurrentState <= IDLE;
        elsif(CLK'event and CLK = '1') then
            CurrentState <= NextState;
        end if;
    end process;

    process(CurrentState, TRIGGER, Ready, FULL, SendAdc, SendTdc, SendScaler,
            AdcGathererBusy, TdcGathererBusy, ScalerGathererBusy)
    begin
        case CurrentState is
            when IDLE =>
                if(TRIGGER = '1') then
                    if(Ready = '1') then
                        NextState <= READ_DATA_SIZE;
                    else
                        NextState <= WAIT_ADC_TDC_SCALER_READY;
                    end if;
                else
                    NextState <= CurrentState;
                end if;
            when WAIT_ADC_TDC_SCALER_READY =>
                if(Ready = '1') then
                    NextState <= READ_DATA_SIZE;
                else
                    NextState <= CurrentState;
                end if;
            when READ_DATA_SIZE =>
                NextState <= STORE_DATA_SIZE;
            when STORE_DATA_SIZE =>
                if(FULL = '1') then
                    NextState <= WAIT_FULL;
                else
                    NextState <= WRITE_DATA_SIZE;
                end if;
            when WAIT_FULL =>
                if(FULL = '1') then
                    NextState <= CurrentState;
                else
                    NextState <= WRITE_DATA_SIZE;
                end if;
            when WRITE_DATA_SIZE =>
                if(SendAdc = '1') then
                    NextState <= START_ADC_GATHERER;
                else
                    NextState <= ADC_READ_COMPLETE;
                end if;
            when START_ADC_GATHERER =>
                NextState <= WAIT_ADC_GATHERER;
            when WAIT_ADC_GATHERER =>
                if(AdcGathererBusy = '1') then
                    NextState <= CurrentState;
                else
                    if(SendTdc = '1') then
                        NextState <= START_TDC_GATHERER;
                    else
                        NextState <= TDC_READ_COMPLETE;
                    end if;
                end if;
            when ADC_READ_COMPLETE =>
                if(SendTdc = '1') then
                    NextState <= START_TDC_GATHERER;
                else
                    NextState <= TDC_READ_COMPLETE;
                end if;
            when START_TDC_GATHERER =>
                NextState <= WAIT_TDC_GATHERER;
            when WAIT_TDC_GATHERER =>
                if(TdcGathererBusy = '1') then
                    NextState <= CurrentState;
                else
                    if(SendScaler = '1') then
                        NextState <= START_SCALER_GATHERER;
                    else
                        NextState <= SCALER_READ_COMPLETE;
                    end if;
                end if;
            when TDC_READ_COMPLETE =>
                if(SendScaler = '1') then
                    NextState <= START_SCALER_GATHERER;
                else
                    NextState <= SCALER_READ_COMPLETE;
                end if;
            when START_SCALER_GATHERER =>
                NextState <= WAIT_SCALER_GATHERER;
            when WAIT_SCALER_GATHERER =>
                if(ScalerGathererBusy = '1') then
                    NextState <= CurrentState;
                else
                    NextState <= IDLE;
                end if;
            when SCALER_READ_COMPLETE =>
                NextState <= IDLE;
        end case;
    end process;

    ADC0_RCOMP <= '1' when(CurrentState = ADC_READ_COMPLETE) else
                  AdcGathererRcomp0;
    ADC1_RCOMP <= '1' when(CurrentState = ADC_READ_COMPLETE) else
                  AdcGathererRcomp1;
    ADC2_RCOMP <= '1' when(CurrentState = ADC_READ_COMPLETE) else
                  AdcGathererRcomp2;
    ADC3_RCOMP <= '1' when(CurrentState = ADC_READ_COMPLETE) else
                  AdcGathererRcomp3;

    TDC_RCOMP_L <= '1' when(CurrentState = TDC_READ_COMPLETE) else
                   TdcGathererRcompLeading;
    TDC_RCOMP_T <= '1' when(CurrentState = TDC_READ_COMPLETE) else
                   TdcGathererRcompTrailing;
    SCALER_RCOMP <= '1' when(CurrentState = SCALER_READ_COMPLETE) else
                    ScalerGathererRcomp;

    AdcTdcScalerSel <= "00" when(CurrentState = WAIT_ADC_GATHERER or
                                 CurrentState = START_ADC_GATHERER) else
                       "01" when(CurrentState = WAIT_TDC_GATHERER or
                                 CurrentState = START_TDC_GATHERER) else
                       "10";
    WriteHeader <= '1' when(CurrentState = WRITE_DATA_SIZE) else '0';
    HeaderEnable <= '1' when(CurrentState = STORE_DATA_SIZE) else '0';
    ReadDataSize <= '1' when(CurrentState = READ_DATA_SIZE) else '0';
    AdcGathererStart <= '1' when(CurrentState = START_ADC_GATHERER) else '0';
    TdcGathererStart <= '1' when(CurrentState = START_TDC_GATHERER) else '0';
    ScalerGathererStart <= '1' when(CurrentState = START_SCALER_GATHERER) else '0';
    BUSY <= '0' when(CurrentState = IDLE) else '1';

end RTL;

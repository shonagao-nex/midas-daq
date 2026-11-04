--------------------------------------------------------------------------------
--! @file   Scaler.vhd
--! @brief  Analog to Digital Converter
--! @author Takehiro Shiozaki
--! @date   2013-11-11
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity Scaler is
    port (
        SCALER_CLK : in std_logic;
        SITCP_CLK : in std_logic;
        RESET : in std_logic;
        RESET_TIMER : out std_logic;

        -- Data input
        DIN : in std_logic_vector(68 downto 0); -- EASIROC TRIGGER 64CH
                                                -- OR32U, OR32L, OR64, 1kHz, 1MHz

        -- Control Interface
        L1_TRIGGER : in std_logic; -- Synchronized with SCALER_CLK
        FAST_CLEAR : in std_logic;
        BUSY : out std_logic;

        -- Gatherer interface
        DOUT : out std_logic_vector(20 downto 0);
        RADDR : in std_logic_vector(6 downto 0);
        RCOMP : in std_logic;
        EMPTY : out std_logic
    );
end entity;

architecture RTL of Scaler is
    component SingleScaler is
        generic (
            G_BITS : integer
        );
        port (
            CLK : in std_logic;
            RESET : in std_logic;
            DIN : in std_logic;
            DOUT : out std_logic_vector(G_BITS - 1 downto 0);
            OVERFLOW : out std_logic
        );
    end component;

    component DoubleBuffer is
        generic (
            G_WIDTH : integer;
            G_DEPTH : integer
        );
        port (
            RESET : in  std_logic;

            WCLK : in  std_logic;
            DIN : in std_logic_vector(G_WIDTH - 1 downto 0);
            WADDR : in std_logic_vector(G_DEPTH - 1 downto 0);
            WE : in std_logic;
            WCOMP : in std_logic;
            DEC_WPTR : in std_logic;
            FULL : out std_logic;

            RCLK : in std_logic;
            DOUT : out std_logic_vector(G_WIDTH - 1 downto 0);
            RADDR : in std_logic_vector(G_DEPTH - 1 downto 0);
            RCOMP : in std_logic;
            EMPTY : out std_logic
        );
    end component;

    type ScalerArray is array (68 downto 0)
        of std_logic_vector(12 downto 0);

    signal CaptureSingleScaler : std_logic;
    signal CapturedScalerCount : ScalerArray;
    signal CapturedScalerOverFlow : std_logic_vector(68 downto 0);

    signal SingleScalerReset : std_logic;
    signal SingleScalerDout : ScalerArray;
    signal SingleScalerOverflow : std_logic_vector(68 downto 0);

    signal Channel : std_logic_vector(6 downto 0);
    signal ChannelCountUp : std_logic;
    signal ChannelCountClear : std_logic;

    signal DoubleBufferWe : std_logic;
    signal DoubleBufferWcomp : std_logic;
    signal DoubleBufferDecWptr : std_logic;
    signal DoubleBufferFull : std_logic;

    signal SelectedDout : std_logic_vector(20 downto 0);

    type State is (IDLE, CAPTURE, RESET_SCALER, TRANSMIT, WAIT_FULL,
                   WRITE_WCOMP, CLEAR_STATE);
    signal CurrentState : State;
    signal NextState : State;
begin
    SINGLE_SCALER_GENERATE: for i in 0 to 68 generate
        SingleScaler_0: SingleScaler
        generic map(
            G_BITS => 13
        )
        port map(
            CLK => SCALER_CLK,
            RESET => SingleScalerReset,
            DIN => DIN(i),
            DOUT => SingleScalerDout(i),
            OVERFLOW => SingleScalerOverflow(i)
        );
    end generate SINGLE_SCALER_GENERATE;

    DoubleBuffer_0: DoubleBuffer
    generic map(
        G_WIDTH => 21,
        G_DEPTH => 7
    )
    port map(
        RESET => RESET,
        WCLK => SCALER_CLK,
        DIN => SelectedDout,
        WADDR => Channel,
        WE => DoubleBufferWe,
        WCOMP => DoubleBufferWcomp,
        DEC_WPTR => DoubleBufferDecWptr,
        FULL => DoubleBufferFull,
        RCLK => SITCP_CLK,
        DOUT => DOUT,
        RADDR => RADDR,
        RCOMP => RCOMP,
        EMPTY => EMPTY
    );

    process(SCALER_CLK)
    begin
        if(SCALER_CLK'event and SCALER_CLK = '1') then
            if(RESET = '1') then
                Channel <= (others => '0');
            else
                if(ChannelCountClear = '1') then
                    Channel <= (others => '0');
                elsif(ChannelCountUp = '1') then
                    if(Channel >= 68) then
                        Channel <= (others => '0');
                    else
                        Channel <= Channel + 1;
                    end if;
                end if;
            end if;
        end if;
    end process;

    process(SCALER_CLK)
    begin
        if(SCALER_CLK'event and SCALER_CLK = '1') then
            if(RESET = '1') then
                CurrentState <= IDLE;
            else
                CurrentState <= NextState;
            end if;
        end if;
    end process;

    process(SCALER_CLK)
    begin
        if(SCALER_CLK'event and SCALER_CLK = '1') then
            if(CaptureSingleScaler = '1') then
                CapturedScalerCount <= SingleScalerDout;
                CapturedScalerOverFlow <= SingleScalerOverflow;
            end if;
        end if;
    end process;

    process(CurrentState, L1_TRIGGER, FAST_CLEAR, Channel, DoubleBufferFull)
    begin
        case CurrentState is
            when IDLE =>
                if(FAST_CLEAR = '1') then
                    NextState <= CLEAR_STATE;
                elsif(L1_TRIGGER = '1') then
                    NextState <= CAPTURE;
                else
                    NextState <= CurrentState;
                end if;
            when CAPTURE =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= RESET_SCALER;
                end if;
            when RESET_SCALER =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                else
                    NextState <= TRANSMIT;
                end if;
            when TRANSMIT =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                elsif(Channel = 68) then
                    if(DoubleBufferFull = '1') then
                        NextState <= WAIT_FULL;
                    else
                        NextState <= WRITE_WCOMP;
                    end if;
                else
                    NextState <= CurrentState;
                end if;
            when WAIT_FULL =>
                if(FAST_CLEAR = '1') then
                    NextState <= IDLE;
                elsif(DoubleBufferFull = '1') then
                    NextState <= CurrentState;
                else
                    NextState <= WRITE_WCOMP;
                end if;
            when WRITE_WCOMP =>
                if(FAST_CLEAR = '1') then
                    NextState <= CLEAR_STATE;
                else
                    NextState <= IDLE;
                end if;
            when CLEAR_STATE =>
                NextState <= IDLE;
        end case;
    end process;

    CaptureSingleScaler <= '1' when(CurrentState = CAPTURE) else
                           '0';
    SingleScalerReset <= '1' when(RESET = '1' or CurrentState = RESET_SCALER) else
                         '0';
    ChannelCountUp <= '1' when(CurrentState = TRANSMIT) else
                      '0';
    ChannelCountClear <= '1' when(CurrentState = IDLE or
                                  CurrentState = CLEAR_STATE) else
                         '0';
    DoubleBufferWe <= '1' when(CurrentState = TRANSMIT) else
                      '0';
    DoubleBufferWcomp <= '1' when(CurrentState = WRITE_WCOMP) else
                         '0';
    DoubleBufferDecWptr <= '1' when(CurrentState = CLEAR_STATE) else
                           '0';
    SelectedDout <= Channel &
                    CapturedScalerOverFlow(conv_integer(Channel)) &
                    CapturedScalerCount(conv_integer(Channel));
    BUSY <= '0' when(CurrentState = IDLE or CurrentState = CLEAR_STATE) else
            '1';

    RESET_TIMER <= '1' when(CurrentState = RESET_SCALER) else
                   '0';

end RTL;

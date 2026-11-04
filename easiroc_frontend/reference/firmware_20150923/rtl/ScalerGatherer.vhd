--------------------------------------------------------------------------------
--! @file   SaclerGatherer.vhd
--! @brief  Gather 68 ch Scaler data to FIFO
--! @author Takehiro Shiozaki
--! @date   2014-08-27
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity ScalerGatherer is
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
end ScalerGatherer;

architecture RTL of ScalerGatherer is
    signal Ready : std_logic;
    signal DoutEnable : std_logic;

    signal int_RADDR : std_logic_vector(6 downto 0);
    signal RaddrCountUp : std_logic;
    signal RaddrCountClear : std_logic;

    type State is (IDLE, WAIT_READY, READ_DATA, STORE_DATA, WAIT_FULL,
                   WRITE_DATA, READ_COMPLETE);
    signal CurrentState : State;
    signal NextState : State;
begin
    process(SITCP_CLK)
    begin
        if(SITCP_CLK'event and SITCP_CLK = '1') then
            if(RESET = '1') then
                int_RADDR <= (others => '0');
            else
                if(RaddrCountClear = '1') then
                    int_RADDR <= (others => '0');
                elsif(RaddrCountUp = '1') then
                    int_RADDR <= int_RADDR + 1;
                end if;
            end if;
        end if;
    end process;
    RADDR <= int_RADDR;

    process(SITCP_CLK)
    begin
        if(SITCP_CLK'event and SITCP_CLK = '1') then
            if(RESET = '1') then
                DOUT <= (others => '0');
            else
                if(DoutEnable = '1') then
                    DOUT <= DIN;
                end if;
            end if;
        end if;
    end process;

    process(SITCP_CLK)
    begin
        if(SITCP_CLK'event and SITCP_CLK = '1') then
            if(RESET = '1') then
                CurrentState <= IDLE;
            else
                CurrentState <= NextState;
            end if;
        end if;
    end process;

    Ready <= not EMPTY;
    process(CurrentState, int_RADDR, Ready, START, FULL)
    begin
        case CurrentState is
            when IDLE =>
                if(START = '1') then
                    if(Ready = '1') then
                        NextState <= READ_DATA;
                    else
                        NextState <= WAIT_READY;
                    end if;
                else
                    NextState <= CurrentState;
                end if;
            when WAIT_READY =>
                if(Ready = '1') then
                    NextState <= READ_DATA;
                else
                    NextState <= CurrentState;
                end if;
            when READ_DATA =>
                NextState <= STORE_DATA;
            when STORE_DATA =>
                if(FULL = '1') then
                    NextState <= WAIT_FULL;
                else
                    NextState <= WRITE_DATA;
                end if;
            when WAIT_FULL =>
                if(FULL = '1') then
                    NextState <= CurrentState;
                else
                    NextState <= WRITE_DATA;
                end if;
            when WRITE_DATA =>
                if(int_RADDR = 68) then
                    NextState <= READ_COMPLETE;
                else
                    NextState <= READ_DATA;
                end if;
            when READ_COMPLETE =>
                NextState <= IDLE;
        end case;
    end process;

    DoutEnable <= '1' when(CurrentState = STORE_DATA) else
                  '0';
    RaddrCountUp <= '1' when(CurrentState = WRITE_DATA) else
                    '0';
    RaddrCountClear <= '1' when(CurrentState = IDLE) else
                       '0';
    WE <= '1' when(CurrentState = WRITE_DATA) else
          '0';
    BUSY <= '0' when(CurrentState = IDLE) else
            '1';
    RCOMP <= '1' when(CurrentState = READ_COMPLETE) else
             '0';
end RTL;

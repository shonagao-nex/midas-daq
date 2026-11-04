--------------------------------------------------------------------------------
--! @file   TDC_Gatherer.vhd
--! @brief  Gatherer 64ch TDC data to FIFO
--! @author Takehiro Shiozaki
--! @date   2014-05-05
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity TDC_Gatherer is
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
end TDC_Gatherer;

architecture RTL of TDC_Gatherer is
    signal Din : std_logic_vector(19 downto 0);
    signal IsFooter : std_logic;
    signal DoutEnable : std_logic;
    signal Ready : std_logic;
    signal Rcomp : std_logic;

    signal Raddr : std_logic_vector(10 downto 0);
    signal RaddrCountUp : std_logic;
    signal RaddrCountClear : std_logic;

    signal DinSel : std_logic;
    signal DinSelCountUp : std_logic;
    signal DinSelCountClear : std_logic;

    type State is (IDLE, WAIT_READY, READ_DATA, STORE_DATA, WAIT_FULL,
                   WRITE_DATA, INC_DIN_SEL, CLEAR_RADDR, READ_COMPLETE);
    signal CurrentState : State;
    signal NextState : State;
begin

    Din <= DIN_L when(DinSel = '0') else
           DIN_T;

    IsFooter <= Din(19);

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            DOUT <= (others => '0');
        elsif(CLK'event and CLK = '1') then
            if(DoutEnable = '1') then
                DOUT <= DIN;
            end if;
        end if;
    end process;

    Ready <= not(EMPTY_L or EMPTY_T);
    RCOMP_L <= Rcomp;
    RCOMP_T <= Rcomp;

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            Raddr <= (others => '0');
        elsif(CLk'event and CLK = '1') then
            if(RaddrCountClear = '1') then
                Raddr <= (others => '0');
            elsif(RaddrCountUp = '1') then
                Raddr <= Raddr + 1;
            end if;
        end if;
    end process;

    RADDR_L <= Raddr;
    RADDR_T <= Raddr;

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            DinSel <= '0';
        elsif(CLK'event and CLK = '1') then
            if(DinSelCountClear = '1') then
                DinSel <= '0';
            elsif(DinSelCountUp = '1') then
                DinSel <= not DinSel;
            end if;
        end if;
    end process;

    process(CLK, RESET)
    begin
        if(RESET = '1') then
            CurrentState <= IDLE;
        elsif(CLK'event and CLK = '1') then
            CurrentState <= NextState;
        end if;
    end process;

    process(CurrentState, START, IsFooter, FULL, Ready, DinSel)
    begin
        case CurrentState is
            when IDLE =>
                if(START = '0') then
                    NextState <= CurrentState;
                else
                    if(Ready = '1') then
                        NextState <= READ_DATA;
                    else
                        NextState <= WAIT_READY;
                    end if;
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
                if(IsFooter = '1') then
                    if(DinSel = '1') then
                        NextState <= READ_COMPLETE;
                    else
                        NextState <= INC_DIN_SEL;
                    end if;
                else
                    if(FULL = '1') then
                        NextState <= WAIT_FULL;
                    else
                        NextState <= WRITE_DATA;
                    end if;
                end if;
            when WAIT_FULL =>
                if(FULL = '1') then
                    NextState <= CurrentState;
                else
                    NextState <= WRITE_DATA;
                end if;
            when WRITE_DATA =>
                NextState <= READ_DATA;
            when INC_DIN_SEL =>
                NextState <= CLEAR_RADDR;
            when CLEAR_RADDR =>
                NextState <= READ_DATA;
            when READ_COMPLETE =>
                NextState <= IDLE;
        end case;
    end process;

    DoutEnable <= '1' when(CurrentState = STORE_DATA) else '0';

    RaddrCountClear <= '1' when(CurrentState = IDLE or
                                CurrentState = CLEAR_RADDR) else
                       '0';
    RaddrCountUp <= '1' when(CurrentState = WRITE_DATA) else '0';

    DinSelCountClear <= '1' when(CurrentState = IDLE) else '0';
    DinSelCountUp <= '1' when(CurrentState = INC_DIN_SEL) else '0';

    WE <= '1' when(CurrentState = WRITE_DATA) else '0';
    Rcomp <= '1' when(CurrentState = READ_COMPLETE) else '0';
    BUSY <= '1' when(CurrentState /= IDLE) else '0';
end RTL;


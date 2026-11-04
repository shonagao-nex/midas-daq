--------------------------------------------------------------------------------
--! @file   ReconfigurationManager.vhd
--! @brief  Manage reconfiguration sequence
--! @author Takehiro Shiozaki
--! @date   2014-07-12
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity ReconfigurationManager is
    generic(
        G_RECONFIGURATION_MANAGER_ADDRESS : std_logic_vector(31 downto 0)
    );
    port(
        SITCP_CLK : in std_logic;
        RESET : in std_logic;
        RBCP_ACT : in std_logic;
        RBCP_ADDR : in std_logic_vector(31 downto 0);
        RBCP_WE : in std_logic;
        RBCP_WD : in std_logic_vector(7 downto 0);
        RBCP_ACK : out std_logic;
        RECONFIGURATION_N : out std_logic
    );
end ReconfigurationManager;

architecture RTL of ReconfigurationManager is
    signal We : std_logic;
    signal Din : std_logic_vector(7 downto 0);

    type State is (IDLE, STATE_M, STATE_MI, STATE_MIW, STATE_MIWA);
    signal CurrentState, NextState : State;
begin

    We <= '1' when(RBCP_ACT = '1' and
                   RBCP_ADDR = G_RECONFIGURATION_MANAGER_ADDRESS and
                   RBCP_WE = '1') else
          '0';
    Din <= RBCP_WD;

    process(SITCP_CLK)
    begin
        if(SITCP_CLK'event and SITCP_CLK = '1') then
            RBCP_ACK <= We;
        end if;
    end process;

    process(SITCP_CLK, RESET)
    begin
        if(RESET = '1') then
            CurrentState <= IDLE;
        elsif(SITCP_CLK'event and SITCP_CLK = '1') then
            CurrentState <= NextState;
        end if;
    end process;

    process(CurrentState, We, Din)
    begin
        if(We = '1') then
            case CurrentState is
                when IDLE =>
                    if(Din = X"4d") then
                        NextState <= STATE_M;
                    else
                        NextState <= IDLE;
                    end if;
                when STATE_M =>
                    if(Din = X"49") then
                        NextState <= STATE_MI;
                    else
                        NextState <= IDLE;
                    end if;
                when STATE_MI =>
                    if(Din = X"57") then
                        NextState <= STATE_MIW;
                    else
                        NextState <= IDLE;
                    end if;
                when STATE_MIW =>
                    if(Din = X"41") then
                        NextState <= STATE_MIWA;
                    else
                        NextState <= IDLE;
                    end if;
                when STATE_MIWA =>
                    NextState <= IDLE;
            end case;
        else
            NextState <= CurrentState;
        end if;
    end process;

    process(SITCP_CLK, RESET)
    begin
        if(RESET = '1') then
            RECONFIGURATION_N <= '1';
        elsif(SITCP_CLK'event and SITCP_CLK = '1') then
            if(CurrentState = STATE_MIWA) then
                RECONFIGURATION_N <= '0';
            else
                RECONFIGURATION_N <= '1';
            end if;
        end if;
    end process;

end RTL;


--------------------------------------------------------------------------------
--! @file   BusyManager.vhd
--! @brief  Manage busy signal
--! @author Takehiro Shiozaki
--! @date   2014-06-18
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity BusyManager is
    port(
        FAST_CLK : in std_logic;
        RESET : in  std_logic;

        HOLD : in std_logic;
        RESET_BUSY : in std_logic;
        BUSY : out std_logic
    );
end BusyManager;

architecture RTL of BusyManager is
begin
    process(FAST_CLK)
    begin
        if(FAST_CLK'event and FAST_CLK = '1') then
            if(RESET_BUSY = '1' or RESET = '1') then
                BUSY <= '0';
            elsif(HOLD = '1') then
                BUSY <= '1';
            end if;
        end if;
    end process;
end RTL;


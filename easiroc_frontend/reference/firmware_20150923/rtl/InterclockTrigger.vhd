--------------------------------------------------------------------------------
--! @file   InterclockTrigger.vhd
--! @brief  Propagate trigger signal across clock region
--! @author Takehiro Shiozaki
--! @date   2014-07-11
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity InterclockTrigger is
    port(
        CLK_IN : in std_logic;
        CLK_OUT : in std_logic;
        RESET : in std_logic;
        TRIGGER_IN : in std_logic;
        TRIGGER_OUT : out std_logic
    );
end InterclockTrigger;

architecture RTL of InterclockTrigger is
    component Synchronizer is
        port(
            CLK : in  std_logic;
            RESET : in  std_logic;
            DIN : in std_logic;
            DOUT : out std_logic
        );
    end component;

    signal TriggerInExtended : std_logic;
    signal SynchTriggerInExtended : std_logic;
    signal ResetTriggerIn : std_logic;
    signal DelayedSynchTriggerInExtended : std_logic;
begin

    process(CLK_IN)
    begin
        if(CLK_IN'event and CLK_IN = '1') then
            if(RESET = '1') then
                TriggerInExtended <= '0';
            else
                if(TRIGGER_IN = '1') then
                    TriggerInExtended <= '1';
                elsif(ResetTriggerIn = '1') then
                    TriggerInExtended <= '0';
                end if;
            end if;
        end if;
    end process;

    Synchronizer_TriggerInExtended: Synchronizer
    port map(
        CLK => CLK_OUT,
        RESET => RESET,
        DIN => TriggerInExtended,
        DOUT => SynchTriggerInExtended
    );

    Synchronizer_ResetTriggerIn: Synchronizer
    port map(
        CLK => CLK_IN,
        RESET => RESET,
        DIN => SynchTriggerInExtended,
        DOUT => ResetTriggerIn
    );

    process(CLK_OUT)
    begin
        if(CLK_OUT'event and CLK_OUT = '1') then
            DelayedSynchTriggerInExtended <= SynchTriggerInExtended;
            TRIGGER_OUT <= (not DelayedSynchTriggerInExtended) and
                           SynchTriggerInExtended;
        end if;
    end process;

end RTL;

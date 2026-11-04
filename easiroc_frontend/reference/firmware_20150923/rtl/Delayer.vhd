--------------------------------------------------------------------------------
--! @file   Delayer.vhd
--! @brief  Delay signal
--! @author Takehiro Shiozaki
--! @date   2014-06-21
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity Delayer is
    generic(
        G_CLK : integer
    );
    port(
        CLK : in  std_logic;
        DIN : in  std_logic;
        DOUT : out  std_logic
    );
end Delayer;

architecture RTL of Delayer is

    signal Dff : std_logic_vector(G_CLK - 1 downto 0);

begin

    process(CLK)
    begin
        if(CLK'event and CLK = '1') then
            Dff(0) <= DIN;
            Dff(G_CLK - 1 downto 1) <= Dff(G_CLK - 2 downto 0);
        end if;
    end process;

    DOUT <= Dff(G_CLK - 1);

end RTL;


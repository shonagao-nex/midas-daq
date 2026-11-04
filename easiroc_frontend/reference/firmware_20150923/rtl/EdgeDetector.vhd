--------------------------------------------------------------------------------
--! @file   EdgeDetector.vhd
--! @brief  Detect rising edge
--! @author Takehiro Shiozaki
--! @date   2013-11-20
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity EdgeDetector is
    port ( CLK : in  std_logic;
           RESET : in  std_logic;
           DIN : in  std_logic;
           DOUT : out  std_logic);
end EdgeDetector;

architecture RTL of EdgeDetector is

	signal q1, q2 : std_logic;

begin

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			q1 <= '0';
			q2 <= '0';
		elsif(CLK'event and CLK = '1') then
			q1 <= DIN;
			q2 <= q1;
		end if;
	end process;

	DOUT <= q1 and not q2;

end RTL;


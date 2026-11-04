--------------------------------------------------------------------------------
--! @file   DFF_1Shot.vhd
--! @brief  generate 1 shot pulse
--! @author Takehiro Shiozaki
--! @date   2013-10-28
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity DFF_1Shot is
    port ( CLK : in  std_logic;
           RESET : in  std_logic;
			  D : in std_logic;
			  EN : in std_logic;
			  Q : out std_logic
			  );
end DFF_1Shot;

architecture RTL of DFF_1Shot is

	signal int_Q : std_logic;

begin

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			int_Q <= '0';
		elsif(CLK'event and CLK = '1') then
			if(int_Q = '1') then
				int_Q <= '0';
			elsif(EN = '1') then
				int_Q <= D;
			end if;
		end if;
	end process;

	Q <= int_Q;

end RTL;


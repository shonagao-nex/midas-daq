--------------------------------------------------------------------------------
--! @file   TestChargeInjection.vhd
--! @brief  Test charge injection
--! @author Naruhiro Chikuma
--! @date   2015-8-2
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity TestChargeInjection is
    port(
	       CLK     : in  std_logic;
	       RST     : in  std_logic;
	       CAL1    : out std_logic;
	       CAL2    : out std_logic
       );
end TestChargeInjection;

architecture RTL of TestChargeInjection is

	signal test_palse : std_logic;
	signal syn_cnt    : std_logic_vector(31 downto 0);

begin

	process(CLK,RST) begin
	    if(CLK'event and CLK = '1') then
		    if(RST = '0') then
			    syn_cnt <= (others => '0');
		    else
			    syn_cnt <= syn_cnt + 1;
		    end if;
	    end if;
	end process;   

	test_palse <= '1' when (syn_cnt(15 downto 0) = X"8000") else '0';
	CAL1 <= 'Z';
	CAL2 <= test_palse;

end RTL;

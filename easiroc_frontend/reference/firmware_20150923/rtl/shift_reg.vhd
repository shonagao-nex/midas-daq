--------------------------------------------------------------------------------
--! @file   shift_reg.vhd
--! @brief  for HV control
--! @author Naruhiro Chikuma
--! @date   2015-7-23
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity shift_reg is
    port(
	       CLK     : in  std_logic;
	       START   : in  std_logic;
	       DIN     : in  std_logic_vector(23 downto 0);
	       WE      : out std_logic;
	       DOUT    : out std_logic;
	       OUT_CLK : out std_logic
       );
end shift_reg;

architecture RTL of shift_reg is

	signal counter : std_logic_vector(4 downto 0);
	signal REG     : std_logic_vector(23 downto 0);

begin
	OUT_CLK <= CLK;

	process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(START = '0') then
			    REG <= (others => '0');
			    counter <= (others => '0');
		    elsif(counter = 24) then
			    REG <= (others => '0');
		    else
			    if(counter = 0) then
				    REG <= DIN;
				    WE <= '1';
			    else
				    REG(23 downto 0) <= (REG(22 downto 0) & '0');
				    WE <= '0';
			    end if;

			    counter <= counter + 1;
		    end if;
		    
		    DOUT <= REG(23);
	    end if;
	end process;   

end RTL;

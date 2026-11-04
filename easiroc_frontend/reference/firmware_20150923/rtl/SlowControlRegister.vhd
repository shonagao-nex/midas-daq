--------------------------------------------------------------------------------
--! @file   SlowControlRegister.vhd
--! @brief  SRAM for SlowControl.vhd
--! @author Takehiro Shiozaki
--! @date   2013-10-28
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity SlowControlRegister is
    port ( -- write
			  WCLK : in  std_logic;
			  DIN : in std_logic_vector(7 downto 0);
			  WADDR : in std_logic_vector(5 downto 0);
			  WE : in std_logic;

			  -- read
			  RCLK : in std_logic;
			  DOUT : out std_logic_vector(7 downto 0);
			  RADDR : in std_logic_vector(5 downto 0)
			  );
end SlowControlRegister;

architecture RTL of SlowControlRegister is

	subtype RamWord is std_logic_vector(7 downto 0);
	type RamArray is array (0 to 63) of RamWord;
	signal RamData : RamArray;

	signal WriteAddress : integer range 0 to 63;
	signal ReadAddress : integer range 0 to 63;

begin

	WriteAddress <= conv_integer(WADDR);
	ReadAddress  <= conv_integer(RADDR);

	process(WCLK)
	begin
		if(WCLK'event and WCLK = '1') then
			if(WE = '1') then
				RamData(WriteAddress) <= DIN;
			end if;
		end if;
	end process;

	process(RCLK)
	begin
		if(RCLK'event and RCLK = '1') then
			DOUT <= RamData(ReadAddress);
		end if;
	end process;

end RTL;


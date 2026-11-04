--------------------------------------------------------------------------------
--! @file   Serializer.vhd
--! @brief  G_BITS bit serializer
--! @author Takehiro Shiozaki
--! @date   2013-10-28
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity Serializer is
	 generic ( G_BITS : integer
	 );
    port ( CLK : in  std_logic;
	        RESET : in std_logic;
			  START : in std_logic;
			  BUSY : out std_logic;
			  DIN : in std_logic_vector(G_BITS - 1 downto 0);
			  DOUT : out std_logic;
			  CLK_OUT : out std_logic
	 );
end Serializer;

architecture RTL of Serializer is

	signal DataBuffer : std_logic_vector(G_BITS - 1 downto 0);
	signal Count : integer range 0 to G_BITS - 1;
	signal CountUp : std_logic;
	signal CountClear : std_logic;

	type State is (IDLE, CLK_LOW, CLK_HIGH);
	signal CurrentState, NextState : State;

begin

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			CurrentState <= IDLE;
		elsif(CLK'event and CLK = '1') then
			CurrentState <= NextState;
		end if;
	end process;

	process(CurrentState, START, Count)
	begin
		case CurrentState is
			when IDLE =>
				if(START = '1') then
					NextState <= CLK_LOW;
				else
					NextState <= CurrentState;
				end if;
			when CLK_LOW =>
				NextState <= CLK_HIGH;
			when CLK_HIGH =>
				if(Count = G_BITS - 1) then
					NextState <= IDLE;
				else
					NextState <= CLK_LOW;
				end if;
		end case;
	end process;

	CountUp <= '1' when CurrentState = CLK_HIGH else
	               '0';

	CountClear <= '1' when CurrentState = IDLE else
					  '0';

	CLK_OUT <= '1' when CurrentState = CLK_HIGH else
	           '0';

	with(CurrentState) select
		Busy <= '1' when CLK_HIGH | CLK_LOW,
		        '0' when others;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			Count <= 0;
		elsif(CLK'event and CLK = '1') then
			if(CountClear = '1') then
				Count <= 0;
			elsif(CountUp = '1') then
				if(Count = G_BITS - 1) then
					Count <= 0;
				else
					Count <= Count + 1;
				end if;
			end if;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			DataBuffer <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(START = '1') then
				DataBuffer <= DIN;
			end if;
		end if;
	end process;

	DOUT <= DataBuffer(Count);

end RTL;

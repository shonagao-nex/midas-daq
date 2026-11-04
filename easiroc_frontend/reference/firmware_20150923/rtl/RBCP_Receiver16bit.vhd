--------------------------------------------------------------------------------
--! @file   RBCP_Receiver16bit.vhd
--! @brief  convert RBCP signal to SRAM write signal(16bit)
--! @author Takehiro Shiozaki
--! @date   2014-04-10
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity RBCP_Receiver16bit is
    generic ( G_ADDR : std_logic_vector(31 downto 0);
	           G_LEN : integer;
				  G_ADDR_WIDTH : integer
				);
    port ( CLK : in  std_logic;
           RESET : in  std_logic;

			  -- RBCP interface
			  RBCP_ACT : in std_logic;
			  RBCP_ADDR : in std_logic_vector(31 downto 0);
			  RBCP_WE : in std_logic;
			  RBCP_WD : in std_logic_vector(7 downto 0);
			  RBCP_ACK : out std_logic;

			  -- output
			  ADDR : out std_logic_vector(G_ADDR_WIDTH - 1 downto 0);
			  WE : out std_logic;
			  WD : out std_logic_vector(15 downto 0)
			  );
end RBCP_Receiver16bit;

architecture RTL of RBCP_Receiver16bit is
	signal UpperByte : std_logic_vector(7 downto 0);
	signal LowerByte : std_logic_vector(7 downto 0);
	signal PrevAddress : std_logic_vector(31 downto 0);

	signal UpperByteEnable : std_logic;
	signal LowerByteEnable : std_logic;
	signal PrevAddressEnable : std_logic;

	type State is (IDLE, RECEIVE_UPPER, WRITE_DATA);
	signal CurrentState, NextState : State;

	signal AddressIsInRange : std_logic;
	signal RbcpActive : std_logic;
	signal OffsettedAddress : std_logic_vector(31 downto 0);
begin

	RbcpActive <= RBCP_ACT and RBCP_WE;
	AddressIsInRange <= '1' when(G_ADDR <= RBCP_ADDR and RBCP_ADDR <= G_ADDR + G_LEN - 1) else '0';
	OffsettedAddress <= RBCP_ADDR - G_ADDR;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			UpperByte <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(UpperByteEnable = '1') then
				UpperByte <= RBCP_WD;
			end if;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			LowerByte <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(LowerByteEnable = '1') then
				LowerByte <= RBCP_WD;
			end if;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			PrevAddress <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(PrevAddressEnable = '1') then
				PrevAddress <= OffsettedAddress;
			end if;
		end if;
	end process;

	WD <= UpperByte & LowerByte;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			RBCP_ACK <= '0';
		elsif(CLK'event and CLK = '1') then
			RBCP_ACK <= AddressIsInRange and RBCP_WE and RBCP_ACT;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			CurrentState <= IDLE;
		elsif(CLK'event and CLK = '1') then
			CurrentState <= NextState;
		end if;
	end process;

	process(CurrentState, AddressIsInRange, RbcpActive, OffsettedAddress, PrevAddress)
	begin
		case CurrentState is
			when IDLE =>
				if(AddressIsInRange = '1' and RbcpActive = '1' and OffsettedAddress(0) = '0') then
					NextState <= RECEIVE_UPPER;
				else
					NextState <= CurrentState;
				end if;
			when RECEIVE_UPPER =>
				if(RbcpActive = '1' and AddressIsInRange = '1') then
					if(OffsettedAddress = PrevAddress + 1) then
						NextState <= WRITE_DATA;
					else
						NextState <= IDLE;
					end if;
				else
					NextState <= CurrentState;
				end if;
			when WRITE_DATA =>
				NextState <= IDLE;
		end case;
	end process;

	UpperByteEnable <= '1' when(CurrentState = IDLE) else '0';
	LowerByteEnable <= '1' when(CurrentState = RECEIVE_UPPER) else '0';
	PrevAddressEnable <= '1' when(CurrentState = IDLE) else '0';

	WE <= '1' when(CurrentState = WRITE_DATA) else '0';
	ADDR <= PrevAddress(G_ADDR_WIDTH downto 1);

end RTL;


--------------------------------------------------------------------------------
--! @file   ReadRegister.vhd
--! @brief  ReadRegister control module
--! @author Takehiro Shiozaki
--! @date   2013-10-28
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use work.Asynch.all;

entity ReadRegister is
	 generic ( G_READ_REGISTER_ADDR : std_logic_vector(31 downto 0) := X"00000000"
	 );
    port ( CLK : in  std_logic;
           RESET : in  std_logic;
			  READ_REGISTER_CLK : in std_logic;

			  -- RBCP Interface
			  RBCP_ACT : in std_logic;
			  RBCP_ADDR : in std_logic_vector(31 downto 0);
			  RBCP_WE : in std_logic;
			  RBCP_WD : in std_logic_vector(7 downto 0);
			  RBCP_ACK : out std_logic;

			  -- ReadRegister Interface
			  SRIN_READ : out std_logic;
			  CLK_READ : out std_logic
			  );
end ReadRegister;

architecture RTL of ReadRegister is

	constant C_READ_REGISTER_ADDR : std_logic_vector(31 downto 0) := G_READ_REGISTER_ADDR;

	component RBCP_Receiver
	generic ( G_ADDR : std_logic_vector(31 downto 0);
	           G_LEN : integer;
				  G_ADDR_WIDTH : integer
				);
	port(
		CLK : in std_logic;
		RESET : in std_logic;
		RBCP_ACT : in std_logic;
		RBCP_ADDR : in std_logic_vector(31 downto 0);
		RBCP_WE : in std_logic;
		RBCP_WD : in std_logic_vector(7 downto 0);
		RBCP_ACK : out std_logic;
		ADDR : out std_logic_vector(G_ADDR_WIDTH -1 downto 0);
		WE : out std_logic;
		WD : out std_logic_vector(7 downto 0)
		);
	end component;

	component Synchronizer
	port(
		CLK : in std_logic;
		RESET : in std_logic;
		DIN : in std_logic;
		DOUT : out std_logic
		);
	end component;

	component PulseExtender
	generic(
		G_WIDTH : integer
		);
	port(
		CLK : in std_logic;
		RESET : in std_logic;
		DIN : in std_logic;
		DOUT : out std_logic
		);
	end component;

	signal Addr : std_logic_vector(0 downto 0);
	signal We : std_logic;
	signal Wd : std_logic_vector(7 downto 0);

	signal ExpandedWe : std_logic;
	signal SyncWe : std_logic;

	signal Channel : std_logic_vector(4 downto 0);

	signal Count : std_logic_vector(4 downto 0);
	signal CountUp : std_logic;
	signal CountClear : std_logic;

	type State is(IDLE, SRIN_HIGH_0, SRIN_HIGH_1, CLK_LOW, CLK_HIGH);
	signal CurrentState, NextState : State;

begin

	RBCP_Receiver_0: RBCP_Receiver
	generic map(
		G_ADDR => C_READ_REGISTER_ADDR,
		G_LEN => 1,
		G_ADDR_WIDTH => 1
	)
	port map(
		CLK => CLK,
		RESET => RESET,
		RBCP_ACT => RBCP_ACT,
		RBCP_ADDR => RBCP_ADDR,
		RBCP_WE => RBCP_WE,
		RBCP_WD => RBCP_WD,
		RBCP_ACK => RBCP_ACK,
		ADDR => Addr,
		WE => We,
		WD => Wd
	);

	PulseExtender_0: PulseExtender
	generic map(
		G_WIDTH => C_SITCP_CLK_TO_SLOWCONTROL_CLK
	)
	port map(
		CLK => CLK,
		RESET => RESET,
		DIN => We,
		DOUT => ExpandedWe
	);

	Synchronizer_0: Synchronizer port map(
		CLK => READ_REGISTER_CLK,
		RESET => RESET,
		DIN => ExpandedWe,
		DOUT => SyncWe
	);

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			Channel <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(We = '1' and addr = "0") then
				Channel <= Wd(4 downto 0);
			end if;
		end if;
	end process;

	process(READ_REGISTER_CLK, RESET)
	begin
		if(RESET = '1') then
			Count <= (others => '0');
		elsif(READ_REGISTER_CLK'event and READ_REGISTER_CLK = '1') then
			if(CountClear = '1') then
				Count <= (others => '0');
			elsif(CountUp = '1') then
				Count <= Count + 1;
			end if;
		end if;
	end process;

	process(READ_REGISTER_CLK, RESET)
	begin
		if(RESET = '1') then
			CurrentState <= IDLE;
		elsif(READ_REGISTER_CLK'event and READ_REGISTER_CLK = '1') then
			CurrentState <= NextState;
		end if;
	end process;

	process(CurrentState, SyncWe, Count, Channel)
	begin
		case CurrentState is
			when IDLE =>
				if(SyncWe = '1') then
					NextState <= SRIN_HIGH_0;
				else
					NextState <= CurrentState;
				end if;
			when SRIN_HIGH_0 =>
				NextState <= SRIN_HIGH_1;
			when SRIN_HIGH_1 =>
				NextState <= CLK_LOW;
			when CLK_LOW =>
				if(Count = Channel + 1) then
					NextState <= IDLE;
				else
					NextState <= CLK_HIGH;
				end if;
			when CLK_HIGH =>
				NextState <= CLK_LOW;
		end case;
	end process;

	with(CurrentState) select
		CountUp <= '1' when SRIN_HIGH_1 | CLK_HIGH,
		           '0' when others;

	with(CurrentState) select
		CountClear <= '1' when IDLE,
		              '0' when others;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			SRIN_READ <= '0';
		elsif(CLK'event and CLK = '1') then
			if(CurrentState = SRIN_HIGH_0 or CurrentState = SRIN_HIGH_1) then
				SRIN_READ <= '1';
			else
				SRIN_READ <= '0';
			end if;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			CLK_READ <= '0';
		elsif(CLK'event and CLK = '1') then
			if(CurrentState = SRIN_HIGH_1 or CurrentState = CLK_HIGH) then
				CLK_READ <= '1';
			else
				CLK_READ <= '0';
			end if;
		end if;
	end process;

end RTL;


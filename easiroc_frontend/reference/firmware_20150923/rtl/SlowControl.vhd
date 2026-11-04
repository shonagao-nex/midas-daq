--------------------------------------------------------------------------------
--! @file   SlowControl.vhd
--! @brief  Slow control register and Probe register control module
--! @author Takehiro Shiozaki
--! @date   2013-10-28
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;

entity SlowControl is
	 generic ( G_SLOW_CONTROL_ADDR : std_logic_vector(31 downto 0));
    port ( CLK : in  std_logic;
           RESET : in  std_logic;
			  SLOW_CONTROL_CLK : in std_logic;

			  -- RBCP Interface
			  RBCP_ACT : in std_logic;
			  RBCP_ADDR : in std_logic_vector(31 downto 0);
			  RBCP_WE : in std_logic;
			  RBCP_WD : in std_logic_vector(7 downto 0);
			  RBCP_ACK : out std_logic;

			  -- Contol Interface
			  START_CYCLE : in std_logic;
			  SELECT_SC : in std_logic;

			  --SlowControl Interface
			  SRIN_SR : out std_logic;
			  CLK_SR : out std_logic
			  );
end SlowControl;

architecture RTL of SlowControl is

	constant C_SLOW_CONTROL_ADDR : std_logic_vector(31 downto 0) := G_SLOW_CONTROL_ADDR;
	constant C_SLOW_CONTROL_LEN : integer := 57;

	constant C_WORDS_TO_TRANSMIT_SLOW_CONTROL : integer := 57;
	constant C_WORDS_TO_TRANSMIT_PROBE : integer := 20;

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

	component SlowControlRegister
	port(
		WCLK : in std_logic;
		DIN : in std_logic_vector(7 downto 0);
		WADDR : in std_logic_vector(5 downto 0);
		WE : in std_logic;
		RCLK : in std_logic;
		RADDR : in std_logic_vector(5 downto 0);
		DOUT : out std_logic_vector(7 downto 0)
		);
	end component;

	component Serializer
	generic(
		G_BITS : integer
		);
	port(
		CLK : in std_logic;
		RESET : in std_logic;
		START : in std_logic;
		DIN : in std_logic_vector(G_BITS - 1 downto 0);
		BUSY : out std_logic;
		DOUT : out std_logic;
		CLK_OUT : out std_logic
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

	signal Address : std_logic_vector(5 downto 0);
	signal We : std_logic;
	signal Wd : std_logic_vector(7 downto 0);

	signal RamDataOut : std_logic_vector(7 downto 0);
	signal RamReadAddr : std_logic_vector(5 downto 0);

	signal SerializerStart : std_logic;
	signal SerializerBusy : std_logic;

	signal SyncStartCycle : std_logic;
	signal SyncSelectSC : std_logic;
	signal SyncSelectSCBuffer : std_logic;

	type State is (IDLE, START_SERIALIZER, WAIT_BUSY);
	signal CurrentState, NextState : State;

	signal Count : integer range 0 to 63;
	signal CountUp : std_logic;
	signal CountClear : std_logic;

	signal WordsToTransmit : integer range 0 to 63;

begin

	RBCP_Receiver_0: RBCP_Receiver
	generic map(
		G_ADDR => C_SLOW_CONTROL_ADDR,
		G_LEN => 57,
		G_ADDR_WIDTH => 6
	)
	port map(
		CLK => CLK,
		RESET => RESET,
		RBCP_ACT => RBCP_ACT,
		RBCP_ADDR => RBCP_ADDR,
		RBCP_WE => RBCP_WE,
		RBCP_WD => RBCP_WD,
		RBCP_ACK => RBCP_ACK,
		ADDR => Address,
		WE => We,
		WD => Wd
	);

	SlowControlRegister_0: SlowControlRegister port map(
		WCLK => CLK,
		DIN => Wd,
		WADDR => Address,
		WE => We,
		RCLK => SLOW_CONTROL_CLK,
		DOUT => RamDataOut,
		RADDR => RamReadAddr
	);

	Serializer_0: Serializer
	generic map(
		G_BITS => 8
	)
	port map(
		CLK => SLOW_CONTROL_CLK,
		RESET => RESET,
		START => SerializerStart,
		BUSY => SerializerBusy,
		DIN => RamDataOut,
		DOUT => SRIN_SR,
		CLK_OUT => CLK_SR
	);

	Synchronizer_StartCycle: Synchronizer port map(
		CLK => SLOW_CONTROL_CLK,
		RESET => RESET,
		DIN => START_CYCLE,
		DOUT => SyncStartCycle
	);

	Synchronizer_SelectSC: Synchronizer port map(
		CLK => SLOW_CONTROL_CLK,
		RESET => RESET,
		DIN => SELECT_SC,
		DOUT => SyncSelectSC
	);

	process(SLOW_CONTROL_CLK, RESET)
	begin
		if(RESET = '1') then
			CurrentState <= IDLE;
		elsif(SLOW_CONTROL_CLK'event and SLOW_CONTROL_CLK = '1') then
			CurrentState <= NextState;
		end if;
	end process;

	process(CurrentState, SyncStartCycle, SerializerBusy, Count, WordsToTransmit)
	begin
		case CurrentState is
			when IDLE =>
				if(SyncStartCycle = '1') then
					NextState <= START_SERIALIZER;
				else
					NextState <= CurrentState;
				end if;
			when START_SERIALIZER =>
				NextState <= WAIT_BUSY;
			when WAIT_BUSY =>
				if(SerializerBusy = '1') then
					NextState <= CurrentState;
				elsif(Count >= WordsToTransmit) then
					NextState <= IDLE;
				else
					NextState <= START_SERIALIZER;
				end if;
		end case;
	end process;

	SerializerStart <= '1' when CurrentState = START_SERIALIZER else
	                   '0';
	CountUp <= '1' when CurrentState = START_SERIALIZER else
	           '0';
	CountClear <= '1' when CurrentState = IDLE else
	              '0';

	process(SLOW_CONTROL_CLK, RESET)
	begin
		if(RESET = '1') then
			Count <= 0;
		elsif(SLOW_CONTROL_CLK'event and SLOW_CONTROL_CLK = '1') then
			if(CountClear = '1') then
				Count <= 0;
			elsif(CountUp = '1') then
				if(Count = 63) then
					Count <= 0;
				else
					Count <= Count + 1;
				end if;
			end if;
		end if;
	end process;

	RamReadAddr <= conv_std_logic_vector(Count, 6);

	process(SLOW_CONTROL_CLK, RESET)
	begin
		if(RESET = '1') then
			SyncSelectSCBuffer <= '0';
		elsif(SLOW_CONTROL_CLK'event and SLOW_CONTROL_CLK = '1') then
			if(SyncStartCycle = '1') then
				SyncSelectSCBuffer <= SyncSelectSC;
			end if;
		end if;
	end process;

	WordsToTransmit <= C_WORDS_TO_TRANSMIT_SLOW_CONTROL when(SyncSelectSCBuffer = '1') else
	                   C_WORDS_TO_TRANSMIT_PROBE;

end RTL;


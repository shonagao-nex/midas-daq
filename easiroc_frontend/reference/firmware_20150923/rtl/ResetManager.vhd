--------------------------------------------------------------------------------
--! @file   ResetManager.vhd
--! @brief  Manage several reset signals
--! @author Takehiro Shiozaki
--! @date   2014-05-09
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

library unisim;
use unisim.vcomponents.all;

entity ResetManager is
    port ( CLK : in  std_logic;

			  --EXT_RESET : in std_logic;-- commented out by N.CHIKUMA 7/21/2015
			  PLL_LOCKED : in std_logic;
			  -- HARD_RESET : in std_logic;
			  -- SOFT_RESET : in std_logic;
			  TCP_OPEN_ACK : in std_logic;

			  L1_RESET : out std_logic;
			  L2_RESET : out std_logic;
			  L3_RESET : out std_logic;
			  L4_RESET : out std_logic
	 );
end ResetManager;

architecture RTL of ResetManager is

	component EdgeDetector
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

	signal Clk_N : std_logic;
	signal TcpOpenAck : std_logic;
	signal TcpOpenAck_N : std_logic;
	signal TcpOpenAckEdge : std_logic;
	signal TcpOpenAckNEdge : std_logic;
	signal TcpOpenAckBothEdge : std_logic;
	signal TcpOpenAckBothEdgeExpanded : std_logic;
	signal DelayedTcpOpenAckBothEdge : std_logic;

	signal ResetManagerReset : std_logic;
	signal int_L1_RESET : std_logic;
	signal int_L2_RESET : std_logic;
	signal int_L3_RESET : std_logic;
	signal int_L4_RESET : std_logic;
begin

	Clk_N <= not CLK;
	TcpOpenAck <= TCP_OPEN_ACK;
	TcpOpenAck_N <= not TCP_OPEN_ACK;

	EdgeDetector_TcpOpecAckEdge: EdgeDetector port map(
		CLK => CLK,
		RESET => ResetManagerReset,
		DIN => TcpOpenAck,
		DOUT => TcpOpenAckEdge
	);

	EdgeDetector_TcpOpecAckNEdge: EdgeDetector port map(
		CLK => CLK,
		RESET => ResetManagerReset,
		DIN => TcpOpenAck_N,
		DOUT => TcpOpenAckNEdge
	);

	TcpOpenAckBothEdge <= TcpOpenAckEdge or TcpOpenAckNEdge;

	PulseExtender_TcpOpenAck: PulseExtender
	generic map(
		G_WIDTH => 3
	)
	port map(
		CLK => CLK,
		RESET => ResetManagerReset,
		DIN => TcpOpenAckBothEdge,
		DOUT => TcpOpenAckBothEdgeExpanded
	);

	process(Clk_N, ResetManagerReset)
	begin
		if(ResetManagerReset = '1') then
			DelayedTcpOpenAckBothEdge <= '0';
		elsif(Clk_N'event and Clk_N = '1') then
			DelayedTcpOpenAckBothEdge <= TcpOpenAckBothEdgeExpanded;
		end if;
	end process;

	ResetManagerReset <= int_L4_RESET;

	--int_L4_RESET <= EXT_RESET or (not PLL_LOCKED);-- commented out by N.CHIKUMA 7/21/2015
	int_L4_RESET <= not PLL_LOCKED;
	int_L3_RESET <= int_L4_RESET;
	int_L2_RESET <= int_L3_RESET;
	int_L1_RESET <= int_L2_RESET or DelayedTcpOpenAckBothEdge;

	L1_RESET <= int_L1_RESET;
	L2_RESET <= int_L2_RESET;
	L3_RESET <= int_L3_RESET;
	L4_RESET <= int_L4_RESET;

end RTL;


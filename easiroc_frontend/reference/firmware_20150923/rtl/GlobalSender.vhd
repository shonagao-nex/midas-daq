--------------------------------------------------------------------------------
--! @file   GlobalSender.vhd
--! @brief  Send 32bit data to SiTCP
--! @author Takehiro Shiozaki
--! @date   2014-05-07
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity GlobalSender is
    port ( CLK : in  std_logic;
           RESET : in  std_logic;

			  -- Gatherer interface
			  DIN : in std_logic_vector(31 downto 0);
			  WE : in std_logic;
			  FULL : out std_logic;

			  -- SiTCP interface
			  TCP_TX_DATA : out std_logic_vector(7 downto 0);
			  TCP_TX_WR : out std_logic;
			  TCP_TX_FULL : in std_logic;
			  TCP_OPEN_ACK : in std_logic
			  );
end GlobalSender;

architecture RTL of GlobalSender is
	component SynchFIFO
	 generic(
			G_WIDTH : integer;
			G_DEPTH : integer
			);
    port(
         CLK : in  std_logic;
         RESET : in  std_logic;
         DIN : in  std_logic_vector(G_WIDTH - 1 downto 0);
         WE : in  std_logic;
         FULL : out  std_logic;
         DOUT : out  std_logic_vector(G_WIDTH - 1 downto 0);
         RE : in  std_logic;
         EMPTY : out  std_logic
        );
    end component;

	component TCP_Sender_32bit
	port(
		CLK : in std_logic;
		RESET : in std_logic;
		DIN : in std_logic_vector(31 downto 0);
		EMPTY : in std_logic;
		AFULL : in std_logic;
		TCP_OPEN_ACK : in std_logic;
		DOUT : out std_logic_vector(7 downto 0);
		RE : out std_logic;
		WE : out std_logic
		);
	end component;

	 signal FifoDout : std_logic_vector(31 downto 0);
	 signal SenderRe : std_logic;
	 signal FifoEmpty : std_logic;
begin

	SynchFIFO_0: SynchFIFO
	generic map (
			G_WIDTH => 32,
			G_DEPTH => 12
	)
	port map (
          CLK => CLK,
          RESET => RESET,
          DIN => DIN,
          WE => WE,
          FULL => FULL,
          DOUT => FifoDout,
          RE => SenderRe,
          EMPTY => FifoEmpty
   );

	TCP_Sender_32bit_0: TCP_Sender_32bit port map(
		CLK => CLK,
		RESET => RESET,
		DIN => FifoDout,
		DOUT => TCP_TX_DATA,
		RE => SenderRe,
		WE => TCP_TX_WR,
		EMPTY => FifoEmpty,
		AFULL => TCP_TX_FULL,
		TCP_OPEN_ACK => TCP_OPEN_ACK
	);

end RTL;

--------------------------------------------------------------------------------
--! @file   SynchFIFO.vhd
--! @brief  Synchronous FIFO
--! @author Takehiro Shiozaki
--! @date   2014-04-28
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity SynchFIFO is
    generic (
		G_WIDTH : integer;
		G_DEPTH : integer
		);
    port ( CLK : in  std_logic;
           RESET : in  std_logic;

			  -- write
			  DIN : in std_logic_vector(G_WIDTH - 1 downto 0);
			  WE : in std_logic;
			  FULL : out std_logic;

			  -- read
			  DOUT : out std_logic_vector(G_WIDTH - 1 downto 0);
			  RE : in std_logic;
			  EMPTY : out std_logic
			  );
end SynchFIFO;

architecture RTL of SynchFIFO is

	component DualPortRam
	 generic(
			G_WIDTH : integer;
			G_DEPTH : integer
			);
    port(
         WCLK : in  std_logic;
         DIN : in  std_logic_vector(G_WIDTH - 1 downto 0);
         WADDR : in  std_logic_vector(G_DEPTH - 1 downto 0);
         WE : in  std_logic;
         RCLK : in  std_logic;
         DOUT : out  std_logic_vector(G_WIDTH - 1 downto 0);
         RADDR : in  std_logic_vector(G_DEPTH - 1 downto 0)
        );
    end component;

	 signal Wptr : std_logic_vector(G_DEPTH downto 0);
	 signal WptrCountUp : std_logic;

	 signal Rptr : std_logic_vector(G_DEPTH downto 0);
	 signal RptrCountUp : std_logic;

	 signal int_FULL : std_logic;
	 signal int_EMPTY : std_logic;

begin

	DualPortRam_0: DualPortRam
	generic map (
			G_WIDTH => G_WIDTH,
			G_DEPTH => G_DEPTH
	)
	port map (
          WCLK => CLK,
          DIN => DIN,
          WADDR => Wptr(G_DEPTH - 1 downto 0),
          WE => WptrCountUp,
          RCLK => CLK,
          DOUT => DOUT,
          RADDR => Rptr(G_DEPTH - 1 downto 0)
   );

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			Wptr <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(WptrCountUp = '1') then
				Wptr <= Wptr + 1;
			end if;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			Rptr <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(RptrCountUp = '1') then
				Rptr <= Rptr + 1;
			end if;
		end if;
	end process;

	WptrCountUp <= WE and not int_FULL;
	RptrCountUp <= RE and not int_EMPTY;

	int_FULL <= '1' when(Wptr(G_DEPTH) = not Rptr(G_DEPTH) and
	                 Wptr(G_DEPTH - 1 downto 0) = Rptr(G_DEPTH - 1 downto 0)) else
	        '0';
	int_EMPTY <= '1' when(Wptr = Rptr) else
	         '0';

	FULL <= int_FULL;
	EMPTY <= int_EMPTY;
end RTL;


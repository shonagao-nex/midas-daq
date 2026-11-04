--------------------------------------------------------------------------------
--! @file   Width_Adjuster.vhd
--! @brief  Control width of triggers
--! @author Naruhiro Chikuma
--! @date   2015-09-18
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

entity Width_Adjuster is
	port(
		CLK         : in  std_logic;
		RESET       : in  std_logic;
		TRIGGER_IN  : in  std_logic;
		TRIGGER_OUT : out std_logic;
		WIDTH_cnt   : in  std_logic_vector(7 downto 0);
		WIDTH_rst   : in  std_logic
	);
end Width_Adjuster;

architecture RTL of Width_Adjuster is

    component Delayer
    generic(
        G_CLK : integer
    );
    port(
        CLK  : in  std_logic;
        DIN  : in  std_logic;
        DOUT : out  std_logic
    );
    end component;

    --component Synchronizer
    --port(
    --    CLK   : in  std_logic;
    --    RESET : in  std_logic;
    --    DIN   : in  std_logic;
    --    DOUT  : out std_logic
    --);
    --end component;
 
    signal trigger_s    : std_logic;
    signal trig_delayed : std_logic;
    signal trig_pulse   : std_logic;
    signal trig_pulse_delayed : std_logic;
    signal delay_reg    : std_logic_vector(99 downto 0);

    signal width_int    : integer range 0 to 99;

begin

	Delayer_0: Delayer
	generic map(
		G_CLK => 2
	)
	port map(
		CLK  => CLK,
		DIN  => TRIGGER_IN,
		DOUT => trig_delayed
	);
	trig_pulse <= TRIGGER_IN and (TRIGGER_IN xor trig_delayed);


	width_int <= conv_integer(WIDTH_cnt) + 3;
	process(RESET,CLK)
	begin
	    if(RESET = '1') then
	        delay_reg <= (others=>'0');        
		elsif(CLK'event and CLK = '1') then
			delay_reg(0) <= trig_pulse;
			delay_reg(99 downto 1) <= delay_reg(98 downto 0);
			trig_pulse_delayed <= delay_reg(width_int - 1);
		end if;
	end process;

	process(RESET,WIDTH_rst,trig_pulse,trig_pulse_delayed,CLK) begin
		if(RESET='1' or WIDTH_rst='1') then
			trigger_s <= '0';
		elsif(trig_pulse='1') then
			trigger_s <= '1';
		elsif(trig_pulse_delayed='1') then
			if(CLK'event and CLK='1') then
				trigger_s <= '0';
			end if;
		end if;
	end process;

	TRIGGER_OUT <= trigger_s;

	--Synchronizer_0: Synchronizer
	--port map(
	--	CLK   => CLK,
	--	RESET => RESET,
	--	DIN   => trigger_s,
	--	DOUT  => TRIGGER_OUT
	--);

end RTL;

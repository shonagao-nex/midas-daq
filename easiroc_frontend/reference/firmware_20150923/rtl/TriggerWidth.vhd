--------------------------------------------------------------------------------
--! @file   TriggerWidth.vhd
--! @brief  Control width of triggers
--! @author Naruhiro Chikuma
--! @date   2015-09-18
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

entity TriggerWidth is
    generic(
        G_TRIGGER_WIDTH_ADDRESS : std_logic_vector(31 downto 0) := X"00000000"
    );
    port(
        CLK          : in  std_logic;
        RESET        : in  std_logic;
        RBCP_ACT     : in  std_logic;
        RBCP_ADDR    : in  std_logic_vector(31 downto 0);
        RBCP_WE      : in  std_logic;
        RBCP_WD      : in  std_logic_vector(7 downto 0);
        RBCP_ACK     : out std_logic;
    	INTERVAL_CLK : in  std_logic;
        TRIGGER_IN1  : in  std_logic_vector(31 downto 0);
        TRIGGER_IN2  : in  std_logic_vector(31 downto 0);
        TRIGGER_OUT1 : out std_logic_vector(31 downto 0);
        TRIGGER_OUT2 : out std_logic_vector(31 downto 0)
    );
end TriggerWidth;

architecture RTL of TriggerWidth is
 
    component Width_Adjuster
		port(
            		CLK         : in  std_logic;
            		RESET       : in  std_logic;
			TRIGGER_IN  : in  std_logic;
			TRIGGER_OUT : out std_logic;
			WIDTH_cnt   : in  std_logic_vector(7 downto 0);
			WIDTH_rst   : in  std_logic
		);
    end component;

    component RBCP_Receiver
		generic (
			G_ADDR : std_logic_vector(31 downto 0);
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

    signal addr_recv  : std_logic_vector(0 downto 0);
    signal wd_recv    : std_logic_vector(7 downto 0);
    signal we_recv    : std_logic;

    signal width_cnt  : std_logic_vector(7 downto 0);
    signal trigger1_s : std_logic_vector(31 downto 0);
    signal trigger2_s : std_logic_vector(31 downto 0);

begin

    RBCP_Receiver_0: RBCP_Receiver
    generic map(
        G_ADDR       => G_TRIGGER_WIDTH_ADDRESS,
        G_LEN        => 1,
        G_ADDR_WIDTH => 1
    )
    port map(
        CLK       => CLK,
        RESET     => RESET,

        RBCP_ACT  => RBCP_ACT,
        RBCP_ADDR => RBCP_ADDR,
        RBCP_WE   => RBCP_WE,
        RBCP_WD   => RBCP_WD,
        RBCP_ACK  => RBCP_ACK,
        ADDR      => addr_recv,
        WE        => we_recv,
        WD        => wd_recv
    );	

    process(CLK)
    begin
        if(CLK'event and CLK = '1') then
            if(RESET = '1') then
		    width_cnt <= (others=>'0');
            else
                if(we_recv = '1') then
			if( addr_recv = 0 ) then
				width_cnt <= wd_recv;
			else
				width_cnt <= (others=>'0');
			end if;
                end if;
            end if;
        end if;
    end process;

    WidthAdjuster_GENERATE: for i in 0 to 31 generate
            Width_Adjuster_0: Width_Adjuster
            port map(
                CLK         => INTERVAL_CLK,
        	RESET       => RESET,
        	TRIGGER_IN  => TRIGGER_IN1(i),
        	TRIGGER_OUT => trigger1_s(i),
        	WIDTH_cnt   => width_cnt,
        	WIDTH_rst   => we_recv
            );
            
            Width_Adjuster_1: Width_Adjuster
            port map(
   	         CLK         => INTERVAL_CLK,
        	 RESET       => RESET,
        	 TRIGGER_IN  => TRIGGER_IN2(i),
        	 TRIGGER_OUT => trigger2_s(i),
		 WIDTH_cnt   => width_cnt,
		      WIDTH_rst  => we_recv
            );
    end generate WidthAdjuster_GENERATE;


    TRIGGER_OUT1 <= TRIGGER_IN1 when width_cnt=0 else trigger1_s;
    TRIGGER_OUT2 <= TRIGGER_IN2 when width_cnt=0 else trigger2_s;    


end RTL;

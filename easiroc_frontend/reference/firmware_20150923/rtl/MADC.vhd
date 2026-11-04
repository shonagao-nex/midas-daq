--------------------------------------------------------------------------------
--! @file   MADC.vhd
--! @brief  Monitor ADC
--! @author Naruhiro Chikuma
--! @date   2015-7-23
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity MADC is
    generic(
   	G_MONITOR_ADC_ADDR : std_logic_vector(31 downto 0) := X"00000000";
   	G_READ_MADC_ADDR   : std_logic_vector(31 downto 0) := X"00000000"
    );	    	
    port(
	CLK        : in  std_logic;
	RST        : in  std_logic;
	
	-- RBCP interface
	RBCP_ACT   : in  std_logic;
	RBCP_ADDR  : in  std_logic_vector(31 downto 0);
	RBCP_WD    : in  std_logic_vector(7 downto 0);
	RBCP_WE    : in  std_logic;
	RBCP_ACK   : out std_logic;
	RBCP_RD    : out std_logic_vector(7 downto 0);
	RBCP_RE    : in  std_logic;

    	-- Monitor ADC
	DOUT_MADC  : in  std_logic;
	DIN_MADC   : out std_logic;
	CS_MADC    : out std_logic;
	SCK_MADC   : out std_logic;
	MUX_EN     : out std_logic_vector(3 downto 0);
	MUX        : out std_logic_vector(3 downto 0)
    );
end MADC;

architecture RTL of MADC is

    constant C_MONITOR_ADC_ADDR : std_logic_vector(31 downto 0) := G_MONITOR_ADC_ADDR;
    constant C_READ_MADC_ADDR   : std_logic_vector(31 downto 0) := G_READ_MADC_ADDR;
    
    component MADC_Core is
	    port(
               CLK       : in  std_logic;
               RST	 : in  std_logic;
               CH_SEL    : in  std_logic_vector(7 downto 0);
               ADC_RECV  : in  std_logic;
               ADC_SEND	 : out std_logic;
               CS_ADC	 : out std_logic;
               MADC_DATA : out std_logic_vector(15 downto 0);
               MADC_CLK	 : out std_logic
      );
    end component;

    component RBCP_Sender is
    	generic(
        	G_ADDR : std_logic_vector(31 downto 0);
	        G_LEN : integer;
        	G_ADDR_WIDTH : integer
    	);
   	port(
 	       CLK : in  std_logic;
 	       RESET : in  std_logic;

 	       -- RBCP interface
 	       RBCP_ACT : in std_logic;
 	       RBCP_ADDR : in std_logic_vector(31 downto 0);
 	       RBCP_RE : in std_logic;
 	       RBCP_RD : out std_logic_vector(7 downto 0);
 	       RBCP_ACK : out std_logic;

 	       -- SRAM interface
 	       ADDR : out std_logic_vector(G_ADDR_WIDTH - 1 downto 0);
 	       RD : in std_logic_vector(7 downto 0)
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

    signal MADC_RST     : std_logic;
    signal state_MADC   : std_logic_vector(7 downto 0);
    signal madc_data    : std_logic_vector(15 downto 0);

    signal RST_cnt      : std_logic_vector(31 downto 0);
    signal madc_clk_r   : std_logic;
    signal mux_r        : std_logic_vector(7 downto 0);
    signal mux_s        : std_logic_vector(7 downto 0);
    
    signal Rd           : std_logic_vector(7 downto 0);

    signal ack_receiver : std_logic;   
    signal ack_sender   : std_logic;   
    signal addr_receiver: std_logic_vector(1 downto 0);
    signal addr_sender  : std_logic_vector(1 downto 0);
    signal we_madc      : std_logic;   
    signal wd_madc      : std_logic_vector(7 downto 0);
    signal rd_madc_s    : std_logic_vector(7 downto 0);
    signal rd_madc      : std_logic_vector(7 downto 0);
    
    signal rst_sw       : std_logic;
    signal cnt_state    : std_logic_vector(31 downto 0);

begin

	RBCP_Receiver_0: RBCP_Receiver
        generic map(
            G_ADDR       => C_MONITOR_ADC_ADDR,
            G_LEN        => 3,
            G_ADDR_WIDTH => 2
        )
        port map(
            CLK       => CLK,
            RESET     => RST,
            RBCP_ACT  => RBCP_ACT,
            RBCP_ADDR => RBCP_ADDR,
            RBCP_WE   => RBCP_WE,
            RBCP_WD   => RBCP_WD,
            RBCP_ACK  => ack_receiver,
            ADDR      => addr_receiver,
            WE        => we_madc,
            WD        => wd_madc
        );

	RBCP_Sender_O: RBCP_Sender
        generic map(
        	G_ADDR       => C_READ_MADC_ADDR,
	        G_LEN        => 2,
        	G_ADDR_WIDTH => 2
    	)
   	    port map(
 	       CLK      => CLK,
 	       RESET    => RST,

 	       -- RBCP interface
 	       RBCP_ACT  => RBCP_ACT, 
 	       RBCP_ADDR => RBCP_ADDR,
 	       RBCP_RE   => RBCP_RE,
 	       RBCP_RD   => RBCP_RD,
 	       RBCP_ACK  => ack_sender,

 	       -- SRAM interface
 	       ADDR      => addr_sender,
 	       RD        => Rd
    	);

	-- Connection to state_MADC => CH_SEL(MADC_Core)
	-- Connection to MUX
	-- MADC_RST control
    	process(CLK,RST) begin
		if(RST = '1') then
			mux_s <= (others => '0');
			state_MADC <= X"FF";
		elsif(CLK'event and CLK = '1') then
			if(we_madc = '1') then
				case addr_receiver is 
					when "00" =>
						state_MADC <= wd_madc;
						rst_sw <= '0';
					when "01" =>
						rst_sw <= '1';
					when "10" =>
						mux_s <= wd_madc;
						rst_sw <= '0';
					when others =>
						rst_sw <= '0';
				end case;
			else
				rst_sw <= '0';
			end if;
	        end if;
	end process;

	-- Connection to RD(input to RBCP_Sender)
	process(CLK,RST) begin
		if(RST = '1') then
			Rd <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			case addr_sender is
				when "00" => 
					Rd <= madc_data(15 downto 8);
				when "01" => 
					Rd <= madc_data(7 downto 0);
				when others => 
					Rd <= (others => '0');
			end case;
		end if;
	end process;	

	--- Counter for Reset
	process(CLK) begin
	    if(CLK'event and CLK = '1') then 		    
		    if(rst_sw = '1') then
			    RST_cnt <= (others => '0');
			    MADC_RST <= '1';
		    else
			    if(RST_cnt < 500) then
				    MADC_RST <= '0';
				    RST_cnt <= RST_cnt + 1;
			    else
				    MADC_RST <= '1';
				    RST_cnt <= X"FFFFFFFF";
			    end if;
		    end if;
	    end if;
	end process;


	MADC_Core_O : MADC_Core
	    port map(
               CLK       => CLK,
               RST	 => MADC_RST,
               CH_SEL    => state_MADC,
               ADC_RECV  => DOUT_MADC,
               ADC_SEND	 => DIN_MADC,
               CS_ADC	 => CS_MADC,
               MADC_DATA => madc_data,
               MADC_CLK	 => SCK_MADC
        ); 

    	MUX_EN <= mux_s(7 downto 4);
    	MUX <= mux_s(3 downto 0);
    	
    	RBCP_ACK <= ack_receiver or ack_sender;


end RTL;


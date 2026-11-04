--------------------------------------------------------------------------------
--! @file   HVControl.vhd
--! @brief  HV Control
--! @author Naruhiro Chikuma
--! @date   2015-7-30
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;


entity HVControl is
    generic(
	    	G_HV_CONTROL_ADDR : std_logic_vector(31 downto 0) := X"00000000"
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

	-- DAC output
	SDI_DAC    : out std_logic;
	SCK_DAC    : out std_logic;
	CS_DAC     : out std_logic;
	HV_EN      : out std_logic;
	
	-- LED control
	DOUT_LED   : out std_logic_vector(15 downto 0)
);
end HVControl;

architecture RTL of HVControl is
	
	
	constant C_HV_CONTROL_ADDR : std_logic_vector(31 downto 0) := G_HV_CONTROL_ADDR;

	component shift_reg is
	        port(
	           CLK     : in  std_logic;
	           START   : in  std_logic;
	           DIN     : in  std_logic_vector(23 downto 0);
	           WE      : out std_logic;
	           DOUT    : out std_logic;
	           OUT_CLK : out std_logic
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

	signal dac_cnt    : std_logic_vector(7 downto 0);
	signal HV_dac1    : std_logic_vector(7 downto 0);
	signal HV_dac2    : std_logic_vector(7 downto 0);
	signal HV_EN_r    : std_logic;
	signal dac_start_s  : std_logic;
		
	signal addr_madc  : std_logic_vector(2 downto 0);   
	signal we_madc    : std_logic;   
	signal wd_madc    : std_logic_vector(7 downto 0);   

	signal up_dac     : std_logic_vector(7 downto 0);   
	signal low_dac    : std_logic_vector(7 downto 0);   
	signal dac_par    : std_logic_vector(23 downto 0);
	signal dac_start  : std_logic;
	signal shutdown   : std_logic;
	signal dout_led_r : std_logic_vector(15 downto 0);

begin
        	
	RBCP_Receiver_0: RBCP_Receiver
        generic map(
            G_ADDR       => C_HV_Control_ADDR,
            G_LEN        => 4,
            G_ADDR_WIDTH => 3
        )
        port map(
            CLK       => CLK,
            RESET     => RST,
            RBCP_ACT  => RBCP_ACT,
            RBCP_ADDR => RBCP_ADDR,
            RBCP_WE   => RBCP_WE,
            RBCP_WD   => RBCP_WD,
            RBCP_ACK  => RBCP_ACK,
            ADDR      => addr_madc,
            WE        => we_madc,
            WD        => wd_madc
        );

	
	process(CLK) begin
	   if(CLK'event and CLK = '1') then
		   if(we_madc = '1') then
			   case addr_madc is
				   when "000" => 
					   up_dac <= wd_madc;
					   shutdown  <= '0';
				   when "001" => 
					   low_dac <= wd_madc;
					   shutdown  <= '0';
				   when "010" =>
					   dac_start <= '1';
					   shutdown  <= '0';
				   when "011" =>
					   shutdown  <= '1';
				   when others =>
					   shutdown  <= '0';
					   dac_start <= '0';
			   end case;
		   else
			   dac_start <= '0';
		   end if;
	   end if;
	end process;

	process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(RST = '1') then
			    dac_par <= ( others => '0');
		    else
			    dac_par(21 downto 14) <= up_dac;
			    dac_par(13 downto 6)  <= low_dac;
		    end if;
	    end if;
	end process;

	-- ## HV operation enable(/shutdown)
	process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(RST = '1' or shutdown = '1') then
			    HV_EN_r <= '0';
		    else
			    HV_EN_r <= '1'; 
		    end if;
	    end if;
	end process;

    	HV_EN <= HV_EN_r;

--- ### DAC Control Start
	process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(dac_start = '1') then
			    dac_cnt <= ( others => '0');
			    dac_start_s <= '0';
		    else
			    if(dac_cnt < 30) then
				    dac_start_s <= '1';
			    else
				    dac_start_s <= '0';
				    dac_cnt <= X"FF";
			    end if;

			    dac_cnt <= dac_cnt + 1;
		    end if;
	    end if;
    	end process;   

	shift_reg_O: shift_reg
	port map(
	           CLK    => CLK,
	           START  => dac_start_s,
	           DIN    => dac_par,
	           WE     => CS_DAC,
	           DOUT   => SDI_DAC,	
	           OUT_CLK=> SCK_DAC
	   );

	process(CLK,RST,shutdown) begin
		if(RST = '1' or shutdown = '1') then
			dout_led_r <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(dac_par(21 downto 14)>0 and dac_par(13 downto 6)> 0) then
				dout_led_r <= dac_par(21 downto 6);
			else
				dout_led_r <= (others => '0');
			end if;
		end if;
	end process;

	DOUT_LED <= dout_led_r;

end RTL;


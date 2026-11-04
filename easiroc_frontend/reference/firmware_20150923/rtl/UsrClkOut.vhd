--------------------------------------------------------------------------------
--! @file   UsrClkOut.vhd
--! @brief  User control for digital clock output
--! @author Naruhiro Chikuma
--! @date   2015-8-31
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity UsrClkOut is
    generic(
   	G_USER_OUTPUT_ADDR : std_logic_vector(31 downto 0) := X"00000000"
    );	    	
    port(
	CLK_25M    : in  std_logic;
	RST        : in  std_logic;
	
	-- RBCP interface
	RBCP_ACT   : in  std_logic;
	RBCP_ADDR  : in  std_logic_vector(31 downto 0);
	RBCP_WD    : in  std_logic_vector(7 downto 0);
	RBCP_WE    : in  std_logic;
	RBCP_ACK   : out std_logic;

    	-- clock in
	CLK_500M   : in  std_logic;
	CLK_125M   : in  std_logic;
	CLK_3M     : in  std_logic;
	
    	-- out
    	DOUT       : out std_logic
    );
end UsrClkOut;

architecture RTL of UsrClkOut is

    constant C_USER_OUTPUT_ADDR : std_logic_vector(31 downto 0) := G_USER_OUTPUT_ADDR;

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

    signal dout_s    : std_logic;
    signal addr_recv : std_logic_vector(0 downto 0);
    signal we_recv   : std_logic;
    signal wd_recv   : std_logic_vector(7 downto 0);
    signal out_sw    : std_logic_vector(7 downto 0);

    signal clk_cnt_rst : std_logic_vector(23 downto 0);
    signal clk_rst     : std_logic_vector(23 downto 0);
    signal CLK_cnt     : std_logic_vector(23 downto 0);
    signal CLK_usr     : std_logic;

begin

	RBCP_Receiver_0: RBCP_Receiver
        generic map(
            G_ADDR       => G_USER_OUTPUT_ADDR,
            G_LEN        => 1,
            G_ADDR_WIDTH => 1
        )
        port map(
            CLK       => CLK_25M,
            RESET     => RST,
            RBCP_ACT  => RBCP_ACT,
            RBCP_ADDR => RBCP_ADDR,
            RBCP_WE   => RBCP_WE,
            RBCP_WD   => RBCP_WD,
            RBCP_ACK  => RBCP_ACK,
            ADDR      => addr_recv,
            WE        => we_recv,
            WD        => wd_recv
        );

	process(CLK_25M,RST) begin
		if(RST = '1') then
			out_sw <= X"00";
		elsif(CLK_25M'event and CLK_25M='1') then
			if( we_recv='1' and addr_recv="0") then
				out_sw <= wd_recv;
			end if;
		end if;
	end process;

	clk_cnt_rst <= X"2DC6BF" when out_sw = X"02" else -- 3,000,000-1 
		       X"0493DF" when out_sw = X"03" else -- 300,000-1
		       X"00752F" when out_sw = X"04" else -- 30,000-1 
		       X"000BB7" when out_sw = X"05" else -- 3,000-1
		       X"00012B" when out_sw = X"06" else -- 300-1
		       X"00001D" when out_sw = X"07" else -- 30-1
		       (others => '0');

	clk_rst <= X"16E360" when out_sw = X"02" else -- 1,500,000
		   X"0249F0" when out_sw = X"03" else -- 150,000
		   X"003A98" when out_sw = X"04" else -- 15,000
		   X"0005DC" when out_sw = X"05" else -- 1,500
		   X"000096" when out_sw = X"06" else -- 150
		   X"00000F" when out_sw = X"07" else -- 15
		   (others => '0');


	process(CLK_3M,RST) begin
		if(RST = '1') then
			CLK_cnt <= (others => '0');
		elsif(CLK_3M'event and CLK_3M='1') then
			CLK_cnt <= CLK_cnt + 1;
			if(CLK_cnt >= clk_cnt_rst) then
				CLK_cnt <= (others => '0');
			end if;
		end if;
	end process;

	process(CLK_3M,RST) begin
		if(RST = '1') then
			CLK_usr    <= '0';
		elsif(CLK_3M'event and CLK_3M='1') then
			if(CLK_cnt < clk_rst) then
				CLK_usr <= '0';
			else
				CLK_usr <= '1';
			end if;
		end if;
	end process;


	dout_s <= '0'      when out_sw = X"00" else
		  '1'      when out_sw = X"01" else
		  CLK_usr  when out_sw = X"02" else  -- 1Hz
		  CLK_usr  when out_sw = X"03" else  -- 10Hz
		  CLK_usr  when out_sw = X"04" else  -- 100Hz
		  CLK_usr  when out_sw = X"05" else  -- 1KHz
		  CLK_usr  when out_sw = X"06" else  -- 10KHz
		  CLK_usr  when out_sw = X"07" else  -- 100KHz
		  CLK_3M   when out_sw = X"08" else  
		  CLK_25M  when out_sw = X"09" else 
		  CLK_125M when out_sw = X"0A" else 
		  CLK_500M when out_sw = X"0B" else 
		  '0';

	DOUT <= dout_s;

end RTL;


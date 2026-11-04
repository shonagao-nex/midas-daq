--------------------------------------------------------------------------------
--! @file   LEDControl.vhd
--! @brief  LED Control
--! @author Naruhiro Chikuma
--! @date   2015-7-30
--------------------------------------------------------------------------------
-- *****************************************************************
-- LED[1]:[8] are pair 1.green 8.red  : LED1
-- LED[2]:[7] are pair 2.green 7.red  : LED2
-- LED[3]:[6] are pair 3.green 6.red  : LED3
-- LED[4]:[5] are pair 4.green 5.red  : LED4
-- *****************************************************************

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity LEDControl is
    port(
               CLK        : in  std_logic;
               TcpOpenAck : in  std_logic;
               RBCP_ADDR  : in  std_logic_vector(31 downto 0);
    	       DIN	  : in  std_logic_vector(15 downto 0);
               BUF1       : in  std_logic;
               Busy       : in  std_logic;
               LED1       : out std_logic;
               LED2       : out std_logic;
               LED3       : out std_logic;
               LED4       : out std_logic;
               LED5       : out std_logic;
               LED6       : out std_logic;
               LED7       : out std_logic;
               LED8       : out std_logic
    );
end LEDControl;

architecture RTL of LEDControl is
        
    signal testLED  : std_logic_vector(3 downto 1);
    signal LED_cnt1 : std_logic_vector(31 downto 0);
    signal LED_cnt2 : std_logic_vector(31 downto 0);
    signal r_LED    : std_logic_vector(2 downto 1);

begin

    process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(TcpOpenAck = '0') then
			    LED_cnt1 <= X"0000000A";  -- 32'd10
		    elsif(BUF1 = '1') then
			    LED_cnt1 <= X"00001388";  -- 32'd5000
		    elsif(LED_cnt1 = 50000) then
			    LED_cnt1 <= X"00009C40";  -- 32'd40000
		    else
			    LED_cnt1 <= LED_cnt1 + 1;
		    end if;
	    end if;
    end process;

    process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(TcpOpenAck = '0') then
			    r_LED(1) <= '0';
		    elsif(LED_cnt1>5000 and LED_cnt1<5500) then
			    r_LED(1) <= '1';
		    elsif(LED_cnt1 > 10000) then
			    r_LED(1) <= '0';
		    end if;
	    end if;
    end process;

    process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(TcpOpenAck = '0') then
			    LED_cnt2 <= X"0000000A"; -- 32'd10
		    elsif(Busy = '1') then
			    LED_cnt2 <= X"00001388"; -- 32'd5000
		    elsif(LED_cnt2 = 50000) then
			    LED_cnt2 <= X"00009C40"; -- 32'd40000
		    else
			    LED_cnt2 <= LED_cnt2 + 1;
		    end if;
	    end if;
    end process;

    process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(TcpOpenAck = '0') then
			    r_LED(2) <= '0';
		    elsif(LED_cnt2>5000 and LED_cnt2<5500) then
			    r_LED(2) <= '1';
		    elsif(LED_cnt2 > 10000) then
			    r_LED(2) <= '0';
		    end if;
	    end if;
    end process;

    LED1 <= '1' when(TcpOpenAck = '1') else '0';
    LED2 <= '0';
    LED3 <= '1' when(r_LED(1) = '1')   else '0';
    LED4 <= '1' when(DIN>2500) else '0';
    LED5 <= '0';
    LED6 <= '0';
    LED7 <= '1' when(r_LED(2) = '1')   else '0';
    LED8 <= '0' when(TcpOpenAck = '1') else '1';


end RTL;


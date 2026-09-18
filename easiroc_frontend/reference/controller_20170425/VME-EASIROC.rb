#!/bin/env ruby

require_relative './ConfigLoader.rb'
require_relative './RBCP.rb'
require 'logger'

class Easiroc <
    Struct.new(:slowControl, :probe, :readRegister,
               :razChn, :valEvt, :resetPA, :pwrOn, :selectSc, :loadSc,
               :rstbSr, :rstbRead)
end

class VmeEasiroc
    def initialize(host, tcp_port, udp_port)
        @host = host
        @tcp_port = tcp_port
        @rbcp = RBCP.new(@host, udp_port)

        reloadSetting
        exitDaqMode
        @sendAdc = true
        @sendTdc = true
        @sendScaler = false
    end

    attr_accessor :easiroc1, :easiroc2
    attr_accessor :sendAdc, :sendTdc, :sendScaler

    def sendDirectControl
        address = directControlAddress

        directControlRegister0 = [
            # MSB
            @easiroc1.rstbRead,
            @easiroc1.rstbSr,
            @easiroc1.loadSc,
            @easiroc1.selectSc,
            @easiroc1.pwrOn,
            @easiroc1.resetPA,
            @easiroc1.valEvt,
            @easiroc1.razChn,
            # LSB
        ]

        directControlRegister1 = [
            # MSB
            @easiroc2.rstbRead,
            @easiroc2.rstbSr,
            @easiroc2.loadSc,
            @easiroc2.selectSc,
            @easiroc2.pwrOn,
            @easiroc2.resetPA,
            @easiroc2.valEvt,
            @easiroc2.razChn,
            # LSB
        ]

        directControlRegister2 = [
            # MSB
            false,
            false,
            false,
            false,
            false,
            false,
            @startCycle2,
            @startCycle1,
            # LSB
        ]

        #directControlRegister2 = [
        #    # MSB
        #    @selectProbe,
        #    @selectHg,
        #    @ledBusy,
        #    @ledReady,
        #    @ledUserOutput,
        #    @userOutput,
        #    @startCycle2,
        #    @startCycle1,
        #    # LSB
        #]

        directControlRegister = [directControlRegister0,
                                 directControlRegister1,
				 directControlRegister2] 
            .map{|i| i.map{|j| j ? 1 : 0}.inject{|a, b| a << 1 | b}}

        $logger.debug("VmeEasiroc::sendDirectControl:" +
                '%08b %08b %08b' % [directControlRegister[0],
                                    directControlRegister[1],
				    directControlRegister[2]])

        @rbcp.write(address, directControlRegister)
    end

    def sendSlowControl
        @easiroc1.selectSc = true
        @easiroc2.selectSc = true
        sendSlowControlSub(@easiroc1.slowControl, @easiroc2.slowControl)
    end

    def sendProbeRegister
        @easiroc1.selectSc = false
        @easiroc2.selectSc = false

        if @easiroc1.probe.all?{|i| i == 0}
            $logger.debug('SelectProbe: EASIROC1 >> OFF')
        else
            $logger.debug('SelectProbe: EASIROC1 >> ON')
        end 
        
	if @easiroc2.probe.all?{|i| i == 0}
            $logger.debug('SelectProbe: EASIROC2 >> OFF')
        else
            $logger.debug('SelectProbe: EASIROC2 >> ON')
        end 

        $logger.debug('Send ProbeRegister')
        sendSlowControlSub(@easiroc1.probe, @easiroc2.probe)
    end

    def resetProbeRegister
        @easiroc1.selectSc = false
        @easiroc2.selectSc = false

        $logger.debug('Reset Probe Register')
        sendSlowControlSub(Array.new.fill(0, 0...20), Array.new.fill(0, 0...20))
    end

    def sendReadRegister
        if @easiroc1.readRegister == -1
            $logger.debug('SelectHg : EASIROC1 >> OFF')
        else
            $logger.debug('SelectHg : EASIROC1 >> ON')
        end
        
        if @easiroc2.readRegister == -1
          $logger.debug('SelectHg : EASIROC2 >> OFF')
        else
          $logger.debug('SelectHg : EASIROC2 >> ON')
        end

        resetReadRegister

        if @easiroc1.readRegister >= 0
            $logger.debug("ReadRegister: #{@easiroc1.readRegister}")
            address = readRegister1Address
            @rbcp.write(address, @easiroc1.readRegister)
        end

        if @easiroc2.readRegister >= 0
            $logger.debug("ReadRegister: #{@easiroc2.readRegister}")
            address = readRegister2Address
            @rbcp.write(address, @easiroc2.readRegister)
        end
    end

    def sendPedestalSuppression
        address = pedestalSuppressionAddress
        pedestalSuppressionValue = @configLoader.toPedestalSuppression['HG'] +
                                   @configLoader.toPedestalSuppression['LG']
        $logger.debug("PedestalSuppression: #{pedestalSuppressionValue}")
        @rbcp.write16bit(address, pedestalSuppressionValue)
    end

    def sendSelectbaleLogic
        address = selectableLogicAddress

        selectableLogic = @configLoader.toSelectableLogic
        $logger.debug("SelectableLogic: #{selectableLogic}")
        @rbcp.write(address, selectableLogic)
    end

    def sendTriggerWidth
        address = triggerWidthAddress

        triggerWidth = @configLoader.toTriggerWidth
        $logger.debug("TriggerWidth: #{triggerWidth}")
        @rbcp.write(address, triggerWidth)
    end

    def sendTriggerValues
        address = triggerValuesAddress

        triggerMode = @configLoader.toTriggerMode
        $logger.debug("TriggerMode: #{triggerMode}")
        @rbcp.write(address, triggerMode)

	triggerDelay = @configLoader.toTriggerDelay
        $logger.debug("TriggerDelay: #{triggerDelay}")
        @rbcp.write(address+1, triggerDelay)
    end


    def sendTimeWindow
        address = timeWindowAddress
        timeWindow = @configLoader.timeWindow
        $logger.info("TimeWIndow: #{timeWindow}")
        @rbcp.write16bit(address, timeWindow)
    end

    def resetPedestalSuppression
        address = pedestalSuppressionAddress
        $logger.debug("reset PedestalSuppression")
        @rbcp.write16bit(address, [0] * 64 * 2)
    end

    def resetReadRegister
        @easiroc1.rstbRead = false
        @easiroc2.rstbRead = false
        sendDirectControl

        @easiroc1.rstbRead = true
        @easiroc2.rstbRead = true
        sendDirectControl
    end

    def reloadSetting
        @configLoader = ConfigLoader.new(
            File.expand_path('yaml/RegisterAttribute.yml', File.dirname(__FILE__)),
            File.expand_path('yaml/RegisterValueAlias.yml', File.dirname(__FILE__)),
            File.expand_path('yaml/DefaultRegisterValue.yml', File.dirname(__FILE__)),
            File.expand_path('yaml/RegisterValue.yml', File.dirname(__FILE__)),
            File.expand_path('yaml/InputDAC.yml', File.dirname(__FILE__)),
            File.expand_path('yaml/PedestalSuppression.yml', File.dirname(__FILE__)),
            File.expand_path('yaml/Calibration.yml',File.dirname(__FILE__))
        )
        @easiroc1 = Easiroc.new
        @easiroc1.slowControl = @configLoader.toEasiroc1SlowControl
        @easiroc1.probe = @configLoader.toProbe1SlowControl
        @easiroc1.readRegister = @configLoader.toReadRegister1
        @easiroc1.razChn = false
        @easiroc1.valEvt = true
        @easiroc1.resetPA = true
        @easiroc1.pwrOn = true
        @easiroc1.selectSc = true
        @easiroc1.loadSc = false
        @easiroc1.rstbSr = true
        @easiroc1.rstbRead = true

        @easiroc2 = Easiroc.new
        @easiroc2.slowControl = @configLoader.toEasiroc2SlowControl
        @easiroc2.probe = @configLoader.toProbe2SlowControl
        @easiroc2.readRegister = @configLoader.toReadRegister2
        @easiroc2.razChn = false
        @easiroc2.valEvt = true
        @easiroc2.resetPA = true
        @easiroc2.pwrOn = true
        @easiroc2.selectSc = true
        @easiroc2.loadSc = false
        @easiroc2.rstbSr = true
        @easiroc2.rstbRead = true

        @startCycle1 = false
        @startCycle2 = false
        #@selectHg = false
        #@selectProbe1 = false 
        #@selectProbe2 = false 
        #@ledReady = false
        #@ledBusy = false
        #@ledUserOutput = false
        #@userOutput = false

        @daqMode = false

        @usrClkOut = @configLoader.toUsrClkOutRegister 
    end

    def summary
        @configLoader.summary
    end

    def version
        address = versionAddress
        combinedData= @rbcp.read(address, 6)

        version = combinedData[0, 2]
        versionMajor = version.getbyte(0) >> 4
        versionMinor = version.getbyte(0) & 0x0f
        versionHotfix = version.getbyte(1) >> 4
        versionPatch = version.getbyte(1) & 0x0f

        decode = Proc.new do |i|
            i.unpack('C*').map{|j| [j >> 4, j & 0x0f]}
                .flatten.inject{|a, b| a * 10 + b}
        end
        year = decode.call(combinedData[2, 2])
        month = decode.call(combinedData[4])
        day = decode.call(combinedData[5])
        [versionMajor, versionMinor, versionHotfix, versionPatch, year, month, day]
    end

    def readEvent(numberToRead)
        @sock = TCPSocket.open(@host, @tcp_port)
        begin
            readAndThrowPreviousData
            enterDaqMode do
                numberToRead.times do
                    header = receiveHeader
                    data = receiveData(header[:dataSize])
                    yield(header, data)
                end
            end
            readAndThrowPreviousData
        ensure
            @sock.close
        end
    end

    def sendMadcControl
	    address = monitorAdcAddress

	    #Set ADC rate to 50Hz
	    @rbcp.write(address,248)
	    sleep 0.01 
	    @rbcp.write(address+1,0)
    end

    def sendShutdownHV
	    address = hvControlAddress
	    
	    #send shutdown signal
	    @rbcp.write(address+3,0);
    end	

    def sendHVControl(value)
        address = hvControlAddress
        hvConst = @configLoader.toHVControl

        if(value<0.00)
                puts "Input value must be positive!"
                return
        end
	
	if(value>90.00)
		puts "Too large input value! Must be smaller than 90.00"
    		return
      	end
          
	hvDAC = (value*hvConst[0] + hvConst[1]).to_i
	puts sprintf("MPPC bias volatage: %.2fV, DAC bit: %d", value, hvDAC)
	
	higher8bit = hvDAC/256
	lower8bit  = hvDAC%256
	
	@rbcp.write(address,higher8bit)    ##Set higher 8bit to FBGA reg
	@rbcp.write(address+1,lower8bit)   ##Set lower 8bit to FPGA reg
	@rbcp.write(address+2,1)           ##Start DAC control
    end

    def readMadc(data)
      address = monitorAdcAddress
      address2 = readMadcAddress
      madcConst = @configLoader.toMadcCalibration

      #Set MADC ch to FPGA reg
      @rbcp.write(address,data)
      sleep 0.01  #wait for ADC ch change #default 0.2
      @rbcp.write(address+1,1)

      #Start to read MADC
      @rbcp.write(address,240)
      sleep 0.05 
      @rbcp.write(address+1,0)

      #Read data
      sleep 0.01 #wait for MADC data #default 1
      higher8bit = @rbcp.read(address2,1)
      lower8bit  = @rbcp.read(address2+1,1)
      dac1 = higher8bit[0].getbyte(0).to_f
      dac2 = lower8bit[0].getbyte(0).to_f
      readDAC = dac1*256 + dac2
      puts "readDAC=#{readDAC}" 

      if ( data == 3 )
        read_madc = madcConst["HV"]*readDAC + madcConst["HVOffset"]
      elsif data == 4
        read_madc = madcConst["Current"]*readDAC
      elsif ( data == 5 || data == 0 )
        read_madc = madcConst["Temperature"]*readDAC/65535/2.4 - 273
      elsif ( data == 1 || data == 2 )
        read_madc = madcConst["InputDac"]*readDAC
      end

      read_madc
    end

    def setCh(num)
	    if(num > 32)
		    puts "Channel number should be 0~31."
		    return
	    end

	    address = monitorAdcAddress
	    mux = 0
	    if( num < 16 )
		    if( num%2 == 0 ) 
			    mux = 199 - num/2
		    else    
			    mux = 207 - num/2
		    end
	    elsif( num < 32 )
		    if( num%2 == 0 ) 
			    mux = 55 - (num-16)/2
		    else    
			    mux = 63 - (num-16)/2
		    end
	    elsif( num == 32 )
		    mux = 0
	    end

	    @rbcp.write(address+2,mux)
	    sleep 0.01 #default 0.2
	    
    end

    def sendUsrClkOutRegister
	    address = usrClkOutAddress
	    @rbcp.write(address,@usrClkOut)
    end

    private
    def directControlAddress
        0x00000000
    end

    def slowControl1Address
        0x00000003
    end
    
    def readRegister1Address
        0x0000003C
    end

    def slowControl2Address
        0x0000003D
    end

    def readRegister2Address
        0x00000076
    end
 
    def statusRegisterAddress
        0x00000077
    end
 
    def selectableLogicAddress
        0x00000078
    end

    def triggerWidthAddress
        0x00000088
    end

    def timeWindowAddress
        0x00000100
    end

    def pedestalSuppressionAddress
        0x00001000
    end

    def hvControlAddress
        0x00010000
    end

    def monitorAdcAddress
        0x00010010
    end

    def readMadcAddress
        0x00010020
    end

    def usrClkOutAddress
        0x00010030
    end

    def triggerValuesAddress
        0x00010100
    end

    def versionAddress
        0xF0000000
    end

    def daqModeBit
        0x01
    end

    def sendAdcBit
        0x02
    end

    def sendTdcBit
        0x04
    end

    def sendScalerBit
        0x08
    end

    def sendSlowControlSub(easiroc1, easiroc2)
        @easiroc1.loadSc = false
        @easiroc1.rstbSr = true
        @easiroc2.loadSc = false
        @easiroc2.rstbSr = true
        @startCycle1 = false
        @startCycle2 = false
        sendDirectControl

        $logger.debug("VmeEasiroc::sendSlowControl:")
        $logger.debug("Easiroc1")
        $logger.debug(easiroc1.map{|i| "        %02X\n" % i}.join)
        address = slowControl1Address
        @rbcp.write(address, easiroc1)

        $logger.debug("VmeEasiroc::sendSlowControl:")
        $logger.debug("Easiroc2")
        $logger.debug(easiroc2.map{|i| "        %02X\n" % i}.join)
        address = slowControl2Address
        @rbcp.write(address, easiroc2)

        @startCycle1 = true
        @startCycle2 = true
        sendDirectControl

        sleep(0.1)

        @easiroc1.loadSc = true
        @easiroc2.loadSc = true
        @startCycle1 = false
        @startCycle2 = false
        sendDirectControl

        @easiroc1.loadSc = false
        @easiroc2.loadSc = false
        sendDirectControl
    end

    def writeStatusRegister(data = 0)
        address = statusRegisterAddress
        if @daqMode
            data |= daqModeBit
        end
        if @sendAdc
            data |= sendAdcBit
        end
        if @sendTdc
            data |= sendTdcBit
        end
        if @sendScaler
            data |= sendScalerBit
        end
        $logger.debug "write status register #{data}"
        @rbcp.write(address, data)
    end

    def enterDaqMode
        $logger.info 'enter DAQ MODE'
        @daqMode = true
        writeStatusRegister
        if block_given?
            begin
                yield
            ensure
                exitDaqMode
            end
        end
    end

    def exitDaqMode
        $logger.info 'exit DAQ MODE'
        @daqMode = false
        writeStatusRegister
    end

    def decodeWord(word)
        normalFrame = 0x80000000
        frame = word & 0x80808080
        raise "Frame Error" unless frame == normalFrame

        ret = ((word & 0x7f000000) >> 3) | ((word & 0x007f0000) >> 2) |
        ((word & 0x00007f00) >> 1) | ((word & 0x0000007f) >> 0)
        ret
    end

    def receiveNbyte(bytes)
        data = "".force_encoding('ASCII-8BIT')
        receiveBytes = 0
        while receiveBytes < bytes
            receivedData = @sock.recv(bytes - receiveBytes)
            data << receivedData
            receiveBytes += receivedData.size
        end
        data
    end

    def receiveHeader
        header = decodeWord(receiveNbyte(4).unpack('N').first)
        isHeader = header[27]
        raise "Frame Error" unless isHeader == 1
        dataSize = header & 0x0fff

        {dataSize: dataSize, header: [header].pack('N')}
    end

    def receiveData(dataSize)
         data = receiveNbyte(4 * dataSize).unpack('N*').map(&method(:decodeWord))
         raise "Frame Error" unless data.all?{|i| i[27] == 0}
         data
    end

    def readAndThrowPreviousData(timeout = 0.1)
        thrownSize = 0
        loop do
            rs, = IO::select([@sock], [], [], timeout)
            break if rs.nil?
            dummy = rs[0].recv(256)
            thrownSize +- dummy.size
        end
    end

    def hg?
        ->(i){ i[21] == 0 && i[19] == 0}
    end

    def lg?
        ->(i){ i[21] == 0 && i[19] == 1}
    end

    def tdc?
        ->(i){ i[21] == 1}
    end

end


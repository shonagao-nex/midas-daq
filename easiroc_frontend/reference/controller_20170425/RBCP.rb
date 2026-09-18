require 'socket'

class RBCPHeader
    READ = 0xC0
    WRITE = 0x80
    def initialize(rw, id, dataLength, address)
        @verType = 0xff
        @cmdFlag = rw & 0xff
        @id = id & 0xff
        @dataLength = dataLength & 0xff
        @address = address & 0xffffffff
    end

    def self.fromBin(str)
        RBCPHeader.new(str[1].unpack('C')[0], str[2].unpack('C')[0], str[3].unpack('C')[0], str[4, 4].unpack('N')[0])
    end

    def to_s
        str = String.new
        if RUBY_VERSION >= '1.9.0'
            str.force_encoding('ASCII-8BIT')
        end
        str << @verType
        str << @cmdFlag
        str << @id
        str << @dataLength
        str << [@address].pack('N')
    end
    attr_accessor :verType, :cmdFlag, :id, :dataLength, :address
end

class RBCPError < Exception; end

class RBCP
    def initialize(host, port)
        @host = host
        @port = port
        @id = 0
    end

    def read(address, dataLength)
        readData = "".encode('ASCII-8BIT')
        while dataLength > 0 do
            dataLengthOnePacket = [dataLength, 255].min
            #puts 'RBCP::read address = %08X, length = %d' % [address,  dataLengthOnePacket] ##DEBUG
            readData << com(RBCPHeader::READ, address, dataLengthOnePacket, '')
            dataLength -= dataLengthOnePacket
            address += dataLengthOnePacket
        end
        readData
    end

    def read8bit(address, dataLength)
        read(address, dataLength).unpack('C*')
    end

    def read16bit(address, dataLength)
        read(address, dataLength * 2).unpack('n*')
    end

    def read32bit(address, dataLength)
        read(address, dataLength * 4).unpack('N*')
    end

    def write(address, data)
        if data.is_a?(Fixnum)
            data = [data]
        end

        if data.is_a?(Array)
            data = data.pack('C*')
        end

        remainingDataLength = data.length
        dataIndex = 0
        while remainingDataLength > 0
            dataLengthOnePacket = [remainingDataLength, 255].min
            dataToWrite = data[dataIndex, dataLengthOnePacket]
            #puts 'RBCP::write address = %08X, data = %02X, length = %d' % [address + dataIndex, dataToWrite.getbyte(0), dataLengthOnePacket] ##DEBUG
            com(RBCPHeader::WRITE, address + dataIndex, dataLengthOnePacket, dataToWrite)
            remainingDataLength -= dataLengthOnePacket
            dataIndex += dataLengthOnePacket
        end
    end

    def write8bit(address, data)
        write(address, data)
    end

    def write16bit(address, data)
        if data.is_a?(Fixnum)
            data = [data]
        end

        write(address, data.pack('n*'))
    end

    def write32bit(address, data)
        if data.is_a?(Fixnum)
            data = [data]
        end

        write(address, data.pack('N*'))
    end

    def com(rw, address, dataLength, data)
        retries = 0
        maxRetries = 3
        begin
            return comSub(rw, address, dataLength, data)
        rescue RBCPError => e
            puts e.message
            retries += 1
            retry if retries < maxRetries
            raise e
        end
    end

    def comSub(rw, address, dataLength, data)
        sock = UDPSocket.open()
        begin
            sock.bind("0.0.0.0", 0)

            header = RBCPHeader.new(rw, @id, dataLength, address)
            if RUBY_VERSION >= '1.9.0'
                data.force_encoding('ASCII-8BIT')
            end
            dataToBeSent = header.to_s + data
            if sock.send(dataToBeSent, 0, @host, @port) != dataToBeSent.length
                raise RBCPError.new("cannot send data")
            end

            # wait for 1 seconds until ACK is received
            sel = IO::select([sock], nil, nil, 1)
            raise RBCPError.new("Timeout") if sel == nil
            receivedData = sock.recv(255+8)
            validate(rw, address, dataLength, data, receivedData)
        ensure
            sock.close
            @id = (@id + 1) & 0xff
        end
        receivedData.slice!(0, 8)
        receivedData
    end
    private :com

    def validate(rw, address, dataLength, data, receivedData)
        header = RBCPHeader.fromBin(receivedData)
        if RUBY_VERSION >= '1.9.0'
            raise RBCPError.new("Invalid Ver Type")   if receivedData.getbyte(0) != 0xff
        else
            raise RBCPError.new("Invalid Ver Type")   if receivedData[0] != 0xff
        end
        if header.cmdFlag != (rw | 0x08)
            if header.cmdFlag & 0x01
                raise RBCPError.new("Bus Error")
            else
                raise RBCPError.new("Invalid CMD Flag")
            end
        end
        raise RBCPError.new("Invalid ID")         if header.id != @id
        raise RBCPError.new("Invalid DataLength") if header.dataLength != dataLength
        raise RBCPError.new("Invalid Address")    if header.address != address
        raise RBCPError.new("Frame Error")        if header.dataLength != receivedData.length - 8
    end
    private :validate
end

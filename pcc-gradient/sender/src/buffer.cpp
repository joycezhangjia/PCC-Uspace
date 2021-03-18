/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

 * Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

 * Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

 * Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 03/12/2011
 *****************************************************************************/

#include <cstring>
#include <cmath>
#include <fstream>
#include "buffer.h"
//jia
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#define random(x) (rand()%x)

using namespace std; 

CSndBuffer::CSndBuffer(const int& size, const int& mss):
		m_BufLock(),
		m_pBlock(NULL),
		m_pFirstBlock(NULL),
		m_pCurrBlock(NULL),
		m_pLastBlock(NULL),
		m_pBuffer(NULL),
		m_iNextMsgNo(1),
		m_iSize(size),
		m_iMSS(mss),
		m_iCount(0)
{
	// initial physical buffer of "size"
    m_iBlockSize = 1456;
	m_pBuffer = new Buffer;
	m_pBuffer->m_pcData = new char [m_iSize * m_iBlockSize];
	m_pBuffer->m_iSize = m_iSize;
	m_pBuffer->m_pNext = m_pBuffer;
	bufferEnder = m_pBuffer;

	// circular linked list for out bound packets
	m_pBlock = new Block;
	m_pBuffer->first_block = m_pBlock;
	Block* pb = m_pBlock;
	int bufferNo = 0;
	for (int i = 1; i < m_iSize; ++ i)
	{
		pb->m_pNext = new Block;
		pb->m_iBufferNo = bufferNo;
		pb = pb->m_pNext;
		bufferNo++;
	}
	pb->m_pNext = m_pBlock;
	pb->m_iBufferNo = bufferNo;
	//blockEnder = pb;

	pb = m_pBlock;
	blockHeader = m_pBlock;
	char* pc = m_pBuffer->m_pcData;
	for (int i = 0; i < m_iSize; ++ i)
	{
		pb->m_pcData = pc;
		pb = pb->m_pNext;
		pc += m_iBlockSize;
	}

	m_pFirstBlock = m_pCurrBlock = m_pLastBlock = m_pBlock;
	FirstBuffer = m_pBuffer;
	FirstBlock = 0;

	//jia
	s_lowest = new S_Block;
	s_lowest ->enq_time = 0;
	s_lowest ->offset = 0;
	s_lowest ->sens = -1;
	s_lowest ->sens_init = -1;
	s_lowest ->s_next = s_lowest;
    s_highest = s_lowest;
	
	S_length = 0;
	S_offset = 0;
	S_first = 0;
	S_leftover = 0;
    
	s_waitforackhead = new S_Block;
	s_waitforackhead ->s_next = s_waitforackhead;
	s_waitforacktail = s_waitforackhead;
/*	Block *test = m_pBlock;
	do {
		cout << "initial " << test->m_iBufferNo << " " << test << endl;
		test = test->m_pNext;
	} while (test != m_pBlock);*/

#ifndef WIN32
pthread_mutex_init(&m_BufLock, NULL);
#else
m_BufLock = CreateMutex(NULL, false, NULL);
#endif
}

CSndBuffer::~CSndBuffer()
{
	Block* pb = m_pBlock->m_pNext;
	while (pb != m_pBlock)
	{
		Block* temp = pb;
		pb = pb->m_pNext;
		delete temp;
	}
	delete m_pBlock;
	m_pBuffer = FirstBuffer->m_pNext;
	while (m_pBuffer != FirstBuffer)
	{
		Buffer* temp = m_pBuffer;
		m_pBuffer = m_pBuffer->m_pNext;
		delete [] temp->m_pcData;
		delete temp;
	}
	delete [] m_pBuffer->m_pcData;

	//jia
	S_Block* sb = s_highest;
	while (sb != s_lowest)
	{
		S_Block* temp = sb;
		sb = sb->s_next;
		delete temp;
	}
	delete s_lowest;
	
	sb = s_waitforackhead;
	while (sb != s_waitforacktail)
	{
		S_Block* temp = sb;
		sb = sb->s_next;
		delete temp;
	}
	delete s_waitforacktail;
    delete sb;

#ifndef WIN32
pthread_mutex_destroy(&m_BufLock);
#else
CloseHandle(m_BufLock);
#endif
}

void CSndBuffer::resizeMSS(int newSize) {
    // need a new lock to lock data structure
    // copy out all the data between currblock and
	CGuard bufferguard(m_BufLock);
    // count the total data size
    int total_data_size = 0;
    //Block *blockBeforeCurrBlock;
    //blockBeforeCurrBlock = m_pFirstBlock;
    //while(blockBeforeCurrBlock != m_pFirstBlock) {
    //    blockBeforeCurrBlock = blockBeforeCurrBlock->m_pNext;
    //}
    Block *blockPointer = m_pCurrBlock;
    while(blockPointer != m_pLastBlock) {
        total_data_size += blockPointer->m_iLength;
        blockPointer = blockPointer->m_pNext;
    }
    cout<<"total_data_size is "<<total_data_size<<endl;
    char* tempDataBuffer = new char[total_data_size];
    total_data_size = 0;
    int numberOfBlocks = 0;
    blockPointer = m_pCurrBlock;
    while(blockPointer != m_pLastBlock) {
	    memcpy(tempDataBuffer + total_data_size, blockPointer->m_pcData, blockPointer->m_iLength);
        total_data_size += blockPointer->m_iLength;
        blockPointer = blockPointer->m_pNext;
        numberOfBlocks++;
    }
    m_iMSS = newSize;
    m_pLastBlock = m_pCurrBlock;
    m_iCount -= numberOfBlocks;

    int len = total_data_size;
	int size = len / m_iMSS;
    int ttl= -1;
    bool order = false;
	if ((len % m_iMSS) != 0)
		size ++;

	// dynamically increase sender buffer
	while (size + m_iCount >= m_iSize)
		increase();

	uint64_t time = CTimer::getTime();
	int32_t inorder = order;
	inorder <<= 29;

	Block* s = m_pLastBlock;
	for (int i = 0; i < size; ++ i)
	{
		int pktlen = len - i * m_iMSS;
		if (pktlen > m_iMSS)
			pktlen = m_iMSS;

		memcpy(s->m_pcData, tempDataBuffer + i * m_iMSS, pktlen);
		s->m_iLength = pktlen;

		s->m_iMsgNo = m_iNextMsgNo | inorder;
		if (i == 0)
			s->m_iMsgNo |= 0x80000000;
		if (i == size - 1)
			s->m_iMsgNo |= 0x40000000;

		s->m_OriginTime = time;
		s->m_iTTL = ttl;

		s = s->m_pNext;
		m_iNextMsgNo ++;
	}
	m_pLastBlock = s;
        delete tempDataBuffer;

	m_iCount += size;

}

void CSndBuffer::addBuffer(const char* data, const int& len, const int& ttl, const bool& order, const double sens_s)
{
//cout <<"ADD BUFFER!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
	CGuard bufferguard(m_BufLock);
	//jia
	bool flag = false;
	if (m_pCurrBlock == m_pLastBlock)
	{
		flag = true;
	}

	int size = len / m_iMSS;
	if ((len % m_iMSS) != 0)
		size ++;

	// dynamically increase sender buffer
	while (size + m_iCount >= m_iSize)
		increase();

	uint64_t time = CTimer::getTime();
	int32_t inorder = order;
	inorder <<= 29;

	Block* s = m_pLastBlock;
	for (int i = 0; i < size; ++ i)
	{
		int pktlen = len - i * m_iMSS;
		if (pktlen > m_iMSS)
			pktlen = m_iMSS;

		memcpy(s->m_pcData, data + i * m_iMSS, pktlen);
		s->m_iLength = pktlen;

		s->m_iMsgNo = m_iNextMsgNo | inorder;
		if (i == 0)
			s->m_iMsgNo |= 0x80000000;
		if (i == size - 1)
			s->m_iMsgNo |= 0x40000000;

		s->m_OriginTime = time;
		s->m_iTTL = ttl;

		s = s->m_pNext;
		m_iNextMsgNo ++;
	}
	m_pLastBlock = s;

	m_iCount += size;

	// Iterate and renew S
	S_Block* current = s_highest;
	const double delta_time = 100000;

	cout<<"NOTICE!!!"<<s_highest<<' '<<s_lowest<<' '<<current;

	while(current != s_lowest)
	{
		int t = (time - current->enq_time) / delta_time;
		int coefficient = pow(2, t);
		current->sens = coefficient * current->sens_init;
		current = current->s_next;
		cout<<"NOTICE!!!"<<coefficient<<' '<<t;
	}

	for(int i=0; i<size; ++i)
	{
		double sens = sens_s;
		//cout<<"I'm in add, size= "<<size<<endl;
		if (sens_s==-1)
		{
			srand((double)clock());
        	sens = double(random(2)+1);
			cout<<sens<<endl;
		}

	    if (flag)
        {
			s_lowest->sens_init = sens;
	        s_lowest->sens = sens;
	    	s_lowest->enq_time = time;
	    	s_lowest->offset = 0;
			s_lowest->s_next = s_lowest;
	    	S_length = 1;
			S_offset = 1;
			s_highest = s_lowest;
			cout<<"flag"<<endl;
    	}
		else
    	{
        	S_Block* temp = new S_Block;
			temp->sens_init = sens;
        	temp->sens = sens;
	        temp->offset = S_offset;
    	    temp->enq_time = time;
        	S_length ++;
			S_offset ++;

        	//sort
	        if (sens >= s_highest->sens)
    	    {
        	    temp->s_next = s_highest;
            	s_highest = temp;
				cout<<"highest offset "<<s_highest->offset<<endl;
				cout<<"highest next offset "<<s_highest->s_next->offset<<endl;
	        }
    	    else if (sens <= s_lowest->sens)
        	{
            	s_lowest->s_next = temp;
	            s_lowest = temp;
    	    }
        	else
        	{
            	S_Block* current = s_highest;   //modified (highest or lowest)
	            while (current != s_lowest)
    	        {
        	        if (current->sens >= sens && current->s_next->sens <= sens)
            	    {
                	    temp->s_next = current->s_next;
                    	current->s_next = temp;
	                    break;
    	            }
					current = current->s_next;
            	}
	        }
		}
		flag = false;
        //cout<<"add "<<S_length << " " <<time<<"   "<< m_iCount<<endl;
	}

	cout<<"add "<<m_iCount<<endl;

/*	m_iNextMsgNo ++;
	if (m_iNextMsgNo == CMsgNo::m_iMaxMsgNo)
		m_iNextMsgNo = 1;*/
}

int CSndBuffer::addBufferFromFile(fstream& ifs, const int& len)
{
	int size = len / m_iMSS;
	if ((len % m_iMSS) != 0)
		size ++;
	// dynamically increase sender buffer
	while (size + m_iCount >= m_iSize)
		increase();

//cout << "TEST " << size << " " << m_iCount << " " << m_iSize << endl;

	Block* s = m_pLastBlock;
	int total = 0;
	for (int i = 0; i < size; ++ i)
	{
		if (ifs.bad() || ifs.fail() || ifs.eof())
			break;

		int pktlen = len - i * m_iMSS;
		if (pktlen > m_iMSS)
			pktlen = m_iMSS;

		ifs.read(s->m_pcData, pktlen);
		if ((pktlen = ifs.gcount()) <= 0)
			break;

		s->m_iLength = pktlen;
		s->m_iTTL = -1;
		s = s->m_pNext;

		total += pktlen;
	}
	m_pLastBlock = s;

	m_iCount += size;

	return total;
}

int CSndBuffer::readData(char** data, int32_t& msgno)
{
	// No data to read
	CGuard bufferguard(m_BufLock);
	if (m_pCurrBlock == m_pLastBlock)
		return 0;
	
	cout<<"ZYX test here."<<endl;
    cout<<"curr "<<m_pCurrBlock<<" last "<<m_pLastBlock<<endl;

	int offset = s_highest->offset;   //future:change m_pCurrBlock

	cout<<"send packet offset "<<offset<<" sensitivity "<<s_highest->sens<<endl;
	//cout<<"curr "<<m_pCurrBlock<<" last "<<m_pLastBlock<<endl;

	/**data = m_pCurrBlock->m_pcData;
	int readlen = m_pCurrBlock->m_iLength;
	msgno = m_pCurrBlock->m_iMsgNo;

	cout<<readlen<<"       "<<msgno<<endl;

	m_pCurrBlock = m_pCurrBlock->m_pNext;
    cout<<"send packet"<<endl;
	cout<<"after curr "<<m_pCurrBlock<<" last "<<m_pLastBlock<<endl;*/

	Block* pb = blockHeader;


    for(int i=0;i<offset;i++)
	{
        pb = pb->m_pNext;
		//cout<<"I'm in read"<<endl;
	}
	
	*data = pb->m_pcData;
	int readlen = pb->m_iLength;
	msgno = pb->m_iMsgNo;

	ofstream filecout("record.txt", ios_base::out | ios_base::in);
	if(!filecout)
    {
        cout<<"can't open file!"<<endl;
    }
	filecout.seekp(0,ios_base::end);  
	filecout<<msgno<<" "<<s_highest->sens_init<<" "<<s_highest->sens<<" "<<s_highest->enq_time<<" ";
	filecout.close();

	//cout<<msgno<<" "<<s_highest<<endl;

	m_pCurrBlock = m_pCurrBlock->m_pNext;
	//cout<<"after curr "<<m_pCurrBlock<<" last "<<m_pLastBlock<<endl;


	s_waitforacktail->s_next = s_highest;
	s_waitforacktail = s_highest;
	S_Block* temp = new S_Block;
	temp = s_highest->s_next;
	s_highest = temp;
	s_waitforacktail ->s_next = s_waitforacktail;
	
    S_length --;
	

	//cout<<"read "<<m_iCount<<endl;
	return readlen;
}

int CSndBuffer::readData(char** data, const int offset, int32_t& msgno, int& msglen)
{
	CGuard bufferguard(m_BufLock);

	// find the block
	int movement = (FirstBlock+offset)/m_pBuffer->m_iSize;
	int blockOffset = (FirstBlock+offset)%m_pBuffer->m_iSize;
	Buffer* buffer_p = FirstBuffer;
	for (int i=0; i<movement; ++i)
	{
		buffer_p = buffer_p->m_pNext;
	}
	Block* p;
	if (movement)
		p = buffer_p->first_block;
	else {
		p = m_pFirstBlock;
		blockOffset = offset;
	}
	for (int i=0; i<blockOffset; ++i)
	{
		p = p->m_pNext;
	}

	if ((p->m_iTTL >= 0) && ((CTimer::getTime() - p->m_OriginTime) / 1000 > (uint64_t)p->m_iTTL))
	{
		//cerr<<"DROP!!!"<<endl;
		msgno = p->m_iMsgNo & 0x1FFFFFFF;

		msglen = 1;
		p = p->m_pNext;
		bool move = false;
		while (msgno == (p->m_iMsgNo & 0x1FFFFFFF))
		{
			if (p == m_pCurrBlock)
				move = true;
			p = p->m_pNext;
			if (move)
				m_pCurrBlock = p;
			msglen ++;
		}

		return -1;
	}

	*data = p->m_pcData;
	int readlen = p->m_iLength;
	msgno = p->m_iMsgNo;
        cout<<"send retransmit packet "<<endl;
	return readlen;
}

void CSndBuffer::ackData(const int& offset)
{
	CGuard bufferguard(m_BufLock);
	cout<<"offset1   "<<offset<<endl;

	//cout<<"ack offset "<<offset<<endl;
    
	S_Block* sb = s_waitforackhead;
	for(int i=0;i<offset;i++)
	{
		/*int s_offset = sb->offset;   //future:change m_pCurrBlock

	    Block* pb = blockHeader;

        for(int j=0;j<s_offset;j++)
		{
        	pb = pb->m_pNext;
		}*/
		//cout<<"I'm in ack"<<endl;
		
		sb = sb->s_next;
	}
	s_waitforackhead = sb;
    

    cout<<"offset2   "<<offset<<endl;
	m_iCount -= offset;
	//cout<<"ack "<<S_length << " " << m_iCount<<endl;
	cout<<"ack "<<m_iCount<<endl;

	CTimer::triggerEvent();


	/*int movement = (FirstBlock+offset)/m_pBuffer->m_iSize;
//	cout << "first buffer " << FirstBuffer << " " << FirstBuffer->first_block << endl;
	for (int i=0; i<movement; ++i) {
		FirstBuffer = FirstBuffer->m_pNext;
//		cout << "first buffer " << FirstBuffer << " " << FirstBuffer->first_block << endl;
	}
	FirstBlock = (FirstBlock+offset)%m_pBuffer->m_iSize;

	int blockOffset;
	if (movement) {
		blockOffset = FirstBlock;
		m_pFirstBlock = FirstBuffer->first_block;
//		cout << "testtt " << FirstBuffer->first_block->m_iBufferNo << endl;
	} else {
		blockOffset = offset;
	}

	for (int i=0; i<blockOffset; ++i)
	{
		m_pFirstBlock = m_pFirstBlock->m_pNext;
	}*/

/*if (m_pFirstBlock == tmp)
	cout << "correct ack\n";
else {
	cout << "error in ackData\n";
	cout << "test " << test << " " << offset << " " << movement << " " << blockOffset << " " << m_pBuffer->m_iSize-1 << " " << FirstBlock << " " << m_pFirstBlock->m_iBufferNo << " " << tmp->m_iBufferNo << endl;
	cout << tmp1 << endl;
	for (int i = 0; i < offset; ++i) {
		tmp1 = tmp1->m_pNext;
		if (tmp1 == FirstBuffer-> first_block)
			cout << "Here\n";
		cout << tmp1 -> m_iBufferNo << " " << tmp1 << endl;
	}
}*/

	m_iCount -= offset;

	cout<<"ack data "<<offset<<" count "<<m_iCount<<endl;

	CTimer::triggerEvent();
}

int CSndBuffer::getCurrBufSize() const
{
	return m_iCount;
}

void CSndBuffer::increase()
{

        int unitsize = m_pBuffer->m_iSize;
	// new physical buffer
	Buffer* nbuf = NULL;
	try
	{
		nbuf  = new Buffer;
		nbuf->m_pcData = new char [unitsize * m_iBlockSize];
	}
	catch (...)
	{
		delete nbuf;
		throw CUDTException(3, 2, 0);
	}
	nbuf->m_iSize = unitsize;
	while (bufferEnder->m_pNext != FirstBuffer)
		bufferEnder = bufferEnder->m_pNext;
//	cout << "increase size at " << bufferEnder->first_block->m_iBufferNo << endl;

	Block* prev_first_block;
	if (m_pFirstBlock->m_iBufferNo) {
//		cout << "firstblock " << m_pFirstBlock->m_iBufferNo << " " << m_pFirstBlock << endl;
		prev_first_block = FirstBuffer->first_block;
//		cout << "prev " << prev_first_block->m_iBufferNo << " " << prev_first_block << endl;
		while (prev_first_block->m_pNext != m_pFirstBlock) {
			prev_first_block = prev_first_block->m_pNext;
//			cout << prev_first_block->m_iBufferNo << " " << prev_first_block << endl;
		}
	} else {
//		cout << "firstblock " << m_pFirstBlock->m_iBufferNo << " " << m_pFirstBlock << endl;
		prev_first_block = bufferEnder->first_block;
//		cout << "prev " << prev_first_block->m_iBufferNo << " " << prev_first_block << endl;
		while (prev_first_block->m_pNext != m_pFirstBlock) {
			prev_first_block = prev_first_block->m_pNext;
//			cout << prev_first_block->m_iBufferNo << " " << prev_first_block << endl;
		}
	}

	// insert the buffer at the end of the buffer list
	nbuf->m_pNext = bufferEnder->m_pNext;
	bufferEnder->m_pNext = nbuf;
	bufferEnder = nbuf;

/*cout << "increase\n";
int counter = 0;
Buffer* buffer_p = FirstBuffer;
while (buffer_p != bufferEnder) {
	cout << counter << endl;
	buffer_p = buffer_p->m_pNext;
	counter++;
}
cout << "end increase\n";*/

	// new packet blocks
	Block* nblk = NULL;
	try
	{
		nblk = new Block;
	}
	catch (...)
	{
		delete nblk;
		throw CUDTException(3, 2, 0);
	}
	Block* pb = nblk, *first_block;
	int bufferNo = m_pFirstBlock->m_iBufferNo;
//	cout << "test " << bufferNo << endl;
//HACK: This is a temp solution, could it break file transfer?
	first_block = NULL;
	for (int i = 1; i < unitsize; ++ i)
	{
		pb->m_pNext = new Block;
		pb->m_iBufferNo = bufferNo;
		if (!bufferNo) {
			first_block = pb;
		}
		pb = pb->m_pNext;
		bufferNo = (bufferNo+1)%unitsize;
	}

	// insert the new blocks onto the existing one
	pb->m_iBufferNo = bufferNo;
	if (!bufferNo) {
		first_block = pb;
	}
	pb->m_pNext = prev_first_block->m_pNext;
   	prev_first_block->m_pNext = nblk;
//   	cout << prev_first_block << " " << m_pFirstBlock << " " << pb << " " << nblk << endl;

	if (m_pFirstBlock->m_iBufferNo) {
		bufferEnder->first_block = FirstBuffer->first_block;
		FirstBuffer->first_block = first_block;
//		cout << "testtttt increase " <<  bufferEnder->first_block->m_iBufferNo << " " << FirstBuffer->first_block->m_iBufferNo << endl;
	} else {
		bufferEnder->first_block = first_block;
//		cout << "testtttt increase " << bufferEnder->first_block->m_iBufferNo << endl;
	}

/*cout << "increase testing" << endl;
Block *tmp = bufferEnder->first_block;
while (tmp != m_pFirstBlock) {
	cout << tmp->m_iBufferNo << " " << tmp << endl;
	tmp = tmp->m_pNext;
}
cout << "end increase testing\n";*/


	// insert the new blocks onto the existing one
	pb = nblk;
	char* pc = nbuf->m_pcData;
	for (int i = 0; i < unitsize; ++ i)
	{
		pb->m_pcData = pc;
		pb = pb->m_pNext;
		pc += m_iBlockSize;
	}

	/*   Block* test_p = m_pFirstBlock;
   for (int i = 0; i < m_iSize+33; ++ i) {
      test_p = test_p->m_pNext;
      if (test_p==bufferEnder->first_block)
        cerr<<"AA "<<i+1<<endl;
   }
	 */

	m_iSize += unitsize;
	//cout<<"Increase!"<<endl;
//	cerr<<"Increase!"<<endl;
	//cout<<m_iSize<<endl;
}

////////////////////////////////////////////////////////////////////////////////

CRcvBuffer::CRcvBuffer(CUnitQueue* queue, const int& bufsize):
		m_pUnit(NULL),
		m_iSize(bufsize),
		m_pUnitQueue(queue),
		m_iStartPos(0),
		m_iLastAckPos(0),
		m_iMaxPos(0),
		m_iNotch(0)
{
	m_pUnit = new CUnit* [m_iSize];
	for (int i = 0; i < m_iSize; ++ i)
		m_pUnit[i] = NULL;
}

CRcvBuffer::~CRcvBuffer()
{
	for (int i = 0; i < m_iSize; ++ i)
	{
		if (NULL != m_pUnit[i])
		{
			m_pUnit[i]->m_iFlag = 0;
			-- m_pUnitQueue->m_iCount;
		}
	}

	delete [] m_pUnit;
}

int CRcvBuffer::addData(CUnit* unit, int offset)
{
	int pos = (m_iLastAckPos + offset) % m_iSize;
	if (offset > m_iMaxPos)
		m_iMaxPos = offset;

	if (NULL != m_pUnit[pos])
		return -1;

	m_pUnit[pos] = unit;

	unit->m_iFlag = 1;
	++ m_pUnitQueue->m_iCount;

	return 0;
}

int CRcvBuffer::readBuffer(char* data, const int& len)
{
	int p = m_iStartPos;
	int lastack = m_iLastAckPos;
	int rs = len;

	while ((p != lastack) && (rs > 0))
	{
		int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
		if (unitsize > rs)
			unitsize = rs;

		memcpy(data, m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
		data += unitsize;

		if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
		{
			CUnit* tmp = m_pUnit[p];
			m_pUnit[p] = NULL;
			tmp->m_iFlag = 0;
			-- m_pUnitQueue->m_iCount;

			if (++ p == m_iSize)
				p = 0;

			m_iNotch = 0;
		}
		else
			m_iNotch += rs;

		rs -= unitsize;
	}

	m_iStartPos = p;
	return len - rs;
}

int CRcvBuffer::readBufferToFile(fstream& ofs, const int& len)
{
	int p = m_iStartPos;
	int lastack = m_iLastAckPos;
	int rs = len;

	while ((p != lastack) && (rs > 0))
	{
		int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
		if (unitsize > rs)
			unitsize = rs;

		ofs.write(m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
		if (ofs.fail())
			break;

		if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
		{
			CUnit* tmp = m_pUnit[p];
			m_pUnit[p] = NULL;
			tmp->m_iFlag = 0;
			-- m_pUnitQueue->m_iCount;

			if (++ p == m_iSize)
				p = 0;

			m_iNotch = 0;
		}
		else
			m_iNotch += rs;

		rs -= unitsize;
	}

	m_iStartPos = p;

	return len - rs;
}

void CRcvBuffer::ackData(const int& len)
{
	m_iLastAckPos = (m_iLastAckPos + len) % m_iSize;
	m_iMaxPos -= len;
	if (m_iMaxPos < 0)
		m_iMaxPos = 0;

	CTimer::triggerEvent();
}

int CRcvBuffer::getAvailBufSize() const
{
	// One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
	return m_iSize - getRcvDataSize() - 1;
}

int CRcvBuffer::getRcvDataSize() const
{
	if (m_iLastAckPos >= m_iStartPos)
		return m_iLastAckPos - m_iStartPos;

	return m_iSize + m_iLastAckPos - m_iStartPos;
}

void CRcvBuffer::dropMsg(const int32_t& msgno)
{
	for (int i = m_iStartPos, n = (m_iLastAckPos + m_iMaxPos) % m_iSize; i != n; i = (i + 1) % m_iSize)
		if ((NULL != m_pUnit[i]) && (msgno == m_pUnit[i]->m_Packet.m_iMsgNo))
			m_pUnit[i]->m_iFlag = 3;
}

int CRcvBuffer::readMsg(char* data, const int& len)
{
	int p, q;
	bool passack;
	if (!scanMsg(p, q, passack))
		return 0;

	int rs = len;
	while (p != (q + 1) % m_iSize)
	{
		int unitsize = m_pUnit[p]->m_Packet.getLength();
		if ((rs >= 0) && (unitsize > rs))
			unitsize = rs;

		if (unitsize > 0)
		{
			memcpy(data, m_pUnit[p]->m_Packet.m_pcData, unitsize);
			data += unitsize;
			rs -= unitsize;
		}

		if (!passack)
		{
			CUnit* tmp = m_pUnit[p];
			m_pUnit[p] = NULL;
			tmp->m_iFlag = 0;
			-- m_pUnitQueue->m_iCount;
		}
		else
			m_pUnit[p]->m_iFlag = 2;

		if (++ p == m_iSize)
			p = 0;
	}

	if (!passack)
		m_iStartPos = (q + 1) % m_iSize;

	return len - rs;
}

int CRcvBuffer::getRcvMsgNum()
{
	int p, q;
	bool passack;
	return scanMsg(p, q, passack) ? 1 : 0;
}

bool CRcvBuffer::scanMsg(int& p, int& q, bool& passack)
{
	// empty buffer
	if ((m_iStartPos == m_iLastAckPos) && (m_iMaxPos <= 0))
		return false;

	//skip all bad msgs at the beginning
	while (m_iStartPos != m_iLastAckPos)
	{
		if (NULL == m_pUnit[m_iStartPos])
		{
			if (++ m_iStartPos == m_iSize)
				m_iStartPos = 0;
			continue;
		}

		if ((1 == m_pUnit[m_iStartPos]->m_iFlag) && (m_pUnit[m_iStartPos]->m_Packet.getMsgBoundary() > 1))
		{
			bool good = true;

			// look ahead for the whole message
			for (int i = m_iStartPos; i != m_iLastAckPos;)
			{
				if ((NULL == m_pUnit[i]) || (1 != m_pUnit[i]->m_iFlag))
				{
					good = false;
					break;
				}

				if ((m_pUnit[i]->m_Packet.getMsgBoundary() == 1) || (m_pUnit[i]->m_Packet.getMsgBoundary() == 3))
					break;

				if (++ i == m_iSize)
					i = 0;
			}

			if (good)
				break;
		}

		CUnit* tmp = m_pUnit[m_iStartPos];
		m_pUnit[m_iStartPos] = NULL;
		tmp->m_iFlag = 0;
		-- m_pUnitQueue->m_iCount;

		if (++ m_iStartPos == m_iSize)
			m_iStartPos = 0;
	}

	p = -1;                  // message head
	q = m_iStartPos;         // message tail
	passack = m_iStartPos == m_iLastAckPos;
	bool found = false;

	// looking for the first message
	for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i <= n; ++ i)
	{
		if ((NULL != m_pUnit[q]) && (1 == m_pUnit[q]->m_iFlag))
		{
			switch (m_pUnit[q]->m_Packet.getMsgBoundary())
			{
			case 3: // 11
			p = q;
			found = true;
			break;

			case 2: // 10
				p = q;
				break;

			case 1: // 01
				if (p != -1)
					found = true;
			}
		}
		else
		{
			// a hole in this message, not valid, restart search
			p = -1;
		}

		if (found)
		{
			// the msg has to be ack'ed or it is allowed to read out of order, and was not read before
			if (!passack || !m_pUnit[q]->m_Packet.getMsgOrderFlag())
				break;

			found = false;
		}

		if (++ q == m_iSize)
			q = 0;

		if (q == m_iLastAckPos)
			passack = true;
	}

	// no msg found
	if (!found)
	{
		// if the message is larger than the receiver buffer, return part of the message
		if ((p != -1) && ((q + 1) % m_iSize == p))
			found = true;
	}

	return found;
}

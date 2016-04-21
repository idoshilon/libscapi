#include "../../include/mid_layer/ElGamalEnc.hpp"

void ElGamalOnGrElSendableData::initFromString(const string & row) {
	auto str_vec = explode(row, ':');
	assert(str_vec.size() == 2);
	cipher1->initFromString(str_vec[0]);
	cipher2->initFromString(str_vec[1]);
}

void ElGamalOnGroupElementEnc::setMembers(shared_ptr<DlogGroup> dlogGroup, mt19937 random) {
	auto ddh = dynamic_pointer_cast<DDH>(dlogGroup);
	//The underlying dlog group must be DDH secure.
	if (ddh == NULL) {
		throw new SecurityLevelException("DlogGroup should have DDH security level");
	}
	dlog = dlogGroup;
	qMinusOne = dlog->getOrder() - 1;
	this->random = random;
}

/**
* Default constructor. Uses the default implementations of DlogGroup, CryptographicHash and SecureRandom.
*/
ElGamalOnGroupElementEnc::ElGamalOnGroupElementEnc() {

	try {
		setMembers(make_shared<OpenSSLDlogECF2m>("K-233"), get_seeded_random());
	}
	catch (...) {
		setMembers(make_shared<OpenSSLDlogZpSafePrime>(), get_seeded_random());
	}
}

/**
* Initializes this ElGamal encryption scheme with (public, private) key pair.
* After this initialization the user can encrypt and decrypt messages.
* @param publicKey should be ElGamalPublicKey.
* @param privateKey should be ElGamalPrivateKey.
* @throws InvalidKeyException if the given keys are not instances of ElGamal keys.
*/
void ElGamalOnGroupElementEnc::setKey(shared_ptr<PublicKey> publicKey, shared_ptr<PrivateKey> privateKey) {
	this->publicKey = dynamic_pointer_cast<ElGamalPublicKey>(publicKey);
	//Key should be ElGamalPublicKey.
	if (this->publicKey == NULL) {
		throw new InvalidKeyException("public key should be an instance of ElGamal public key");
	}

	if (privateKey != NULL) {
		auto key = dynamic_pointer_cast<ElGamalPrivateKey>(privateKey);
		//Key should be ElGamalPrivateKey.
		if (key == NULL) {
			throw new InvalidKeyException("private key should be an instance of ElGamal private key");
		}

		/**
		* ElGamal decrypt function can be optimized if, instead of using the x value in the private key as is,
		* we change it to be q-x, while q is the dlog group order.
		* This function computes this changing and saves the new private value as the private key member.
		* @param privateKey to change.
		*/

		//Gets the a value from the private key.
		biginteger x = key->getX();
		//Gets the q-x value.
		biginteger xInv = dlog->getOrder() - x;
		//Sets the q-x value as the private key.
		this->privateKey = make_shared<ElGamalPrivateKey>(xInv);	
	}

	keySet = true;
}

/**
* Generates a KeyPair containing a set of ElGamalPublicKEy and ElGamalPrivateKey using the source of randomness and the dlog specified upon construction.
* @return KeyPair contains keys for this ElGamal object.
*/
pair<shared_ptr<PublicKey>, shared_ptr<PrivateKey>> ElGamalOnGroupElementEnc::generateKey() {

	//Chooses a random value in Zq.
	biginteger x = getRandomInRange(0, qMinusOne, random);
	auto generator = dlog->getGenerator();
	//Calculates h = g^x.
	auto h = dlog->exponentiate(generator.get(), x);
	//Creates an ElGamalPublicKey with h and ElGamalPrivateKey with x.
	auto publicKey = make_shared<ElGamalPublicKey>(h);
	auto privateKey = make_shared<ElGamalPrivateKey>(x);
	//Creates a KeyPair with the created keys.
	return pair<shared_ptr<PublicKey>, shared_ptr<PrivateKey>>(publicKey, privateKey);
}

shared_ptr<PublicKey> ElGamalOnGroupElementEnc::reconstructPublicKey(shared_ptr<KeySendableData> data) {
	auto data1 = dynamic_pointer_cast<ElGamalPublicKeySendableData>(data);
	if (data1 == NULL)
		throw invalid_argument("To generate the key from sendable data, the data has to be of type ScElGamalPublicKeySendableData");
	
	auto h = dlog->reconstructElement(true, data1->getC().get());
	return make_shared<ElGamalPublicKey>(h);
}

shared_ptr<PrivateKey> ElGamalOnGroupElementEnc::reconstructPrivateKey(shared_ptr<KeySendableData> data) {
	auto data1 = dynamic_pointer_cast<ElGamalPrivateKey>(data);
	if (data1 == NULL)
		throw invalid_argument("To generate the key from sendable data, the data has to be of type ElGamalPrivateKey");
	return data1;
}

/**
* Encrypts the given message using ElGamal encryption scheme.
*
* @param plaintext contains message to encrypt. The given plaintext must match this ElGamal type.
* @return Ciphertext containing the encrypted message.
* @throws IllegalStateException if no public key was set.
* @throws IllegalArgumentException if the given Plaintext does not match this ElGamal type.
*/
shared_ptr<AsymmetricCiphertext> ElGamalOnGroupElementEnc::encrypt(shared_ptr<Plaintext> plaintext) {
	// If there is no public key can not encrypt, throws exception.
	if (!isKeySet()) {
		throw new IllegalStateException("in order to encrypt a message this object must be initialized with public key");
	}

	/*
	* Pseudo-code:
	* 		Choose a random  y <- Zq.
	*		Calculate c1 = g^y mod p //Mod p operation are performed automatically by the group.
	*		Calculate c2 = h^y * plaintext.getElement() mod p // For ElGamal on a GroupElement.
	*					OR KDF(h^y) XOR plaintext.getBytes()  // For ElGamal on a ByteArray.
	*/
	//Chooses a random value y<-Zq.
	biginteger y = getRandomInRange(0, qMinusOne, random);

	return encrypt(plaintext, y);
}

/**
* Encrypts the given plaintext using this asymmetric encryption scheme and using the given random value.<p>
* There are cases when the random value is used after the encryption, for example, in sigma protocol.
* In these cases the random value should be known to the user. We decided not to have function that return it to the user
* since this can cause problems when more than one value is being encrypt.
* Instead, we decided to have an additional encrypt value that gets the random value from the user.
*
* @param plaintext contains message to encrypt. The given plaintext must match this ElGamal type.
* @param r The random value to use in the encryption.
* @return Ciphertext containing the encrypted message.
* @throws IllegalStateException if no public key was set.
* @throws IllegalArgumentException if the given Plaintext does not match this ElGamal type.
*/
shared_ptr<AsymmetricCiphertext> ElGamalOnGroupElementEnc::encrypt(shared_ptr<Plaintext> plaintext, biginteger r) {

	/*
	* Pseudo-code:
	*		Calculate c1 = g^r mod p //Mod p operation are performed automatically by the group.
	*		Calculate c2 = h^r * plaintext.getElement() mod p // For ElGamal on a GroupElement.
	*					OR KDF(h^r) XOR plaintext.getBytes()  // For ElGamal on a ByteArray.
	*/

	// If there is no public key can not encrypt, throws exception.
	if (!isKeySet()) {
		throw new IllegalStateException("in order to encrypt a message this object must be initialized with public key");
	}

	auto plain = dynamic_pointer_cast<GroupElementPlaintext>(plaintext);
	if (plain == NULL) {
		throw invalid_argument("plaintext should be instance of GroupElementPlaintext");
	}

	//Check that the r random value passed to this function is in Zq.
	if (!((r >= 0) && (r <= qMinusOne))) {
		throw invalid_argument("r must be in Zq");
	}

	//Calculates c1 = g^y and c2 = msg * h^y.
	auto generator = dlog->getGenerator();
	auto c1 = dlog->exponentiate(generator.get(), r);
	auto hy = dlog->exponentiate(publicKey->getH().get(), r);

	//Gets the element.
	auto msgElement = plain->getElement();

	auto c2 = dlog->multiplyGroupElements(hy.get(), msgElement.get());

	//Returns an ElGamalCiphertext with c1, c2.
	return make_shared<ElGamalOnGroupElementCiphertext>(c1, c2);
}

/**
* Generates a Plaintext suitable to ElGamal encryption scheme from the given message.
* @param text byte array to convert to a Plaintext object.
* @throws IllegalArgumentException if the given message's length is greater than the maximum.
*/
shared_ptr<Plaintext> ElGamalOnGroupElementEnc::generatePlaintext(vector<byte> text) {
	if (text.size() > getMaxLengthOfByteArrayForPlaintext()) {
		throw invalid_argument("the given text is too big for plaintext");
	}

	return make_shared<GroupElementPlaintext>(dlog->encodeByteArrayToGroupElement(text));
}

/**
* Decrypts the given ciphertext using ElGamal encryption scheme.
*
* @param cipher MUST be of type ElGamalOnGroupElementCiphertext contains the cipher to decrypt.
* @return Plaintext of type GroupElementPlaintext which containing the decrypted message.
* @throws KeyException if no private key was set.
* @throws IllegalArgumentException if the given cipher is not instance of ElGamalOnGroupElementCiphertext.
*/
shared_ptr<Plaintext> ElGamalOnGroupElementEnc::decrypt(shared_ptr<AsymmetricCiphertext> cipher) {
	/*
	* Pseudo-code:
	* 		Calculate s = ciphertext.getC1() ^ x^(-1) //x^(-1) is kept in the private key because of the optimization computed in the function initPrivateKey.
	*		Calculate m = ciphertext.getC2() * s
	*/

	//If there is no private key, throws exception.
	if (privateKey == NULL) {
		throw KeyException("in order to decrypt a message, this object must be initialized with private key");
	}

	//Ciphertext should be ElGamal ciphertext.
	auto ciphertext = dynamic_pointer_cast<ElGamalOnGroupElementCiphertext>(cipher);
	if (ciphertext == NULL) {
		throw invalid_argument("ciphertext should be instance of ElGamalOnGroupElementCiphertext");
	}

	//Calculates sInv = ciphertext.getC1() ^ x.
	auto sInv = dlog->exponentiate(ciphertext->getC1().get(), privateKey->getX());
	//Calculates the plaintext element m = ciphertext.getC2() * sInv.
	auto m = dlog->multiplyGroupElements(ciphertext->getC2().get(), sInv.get());

	//Creates a plaintext object with the element and returns it.
	return make_shared<GroupElementPlaintext>(m);
}

/**
* Generates a byte array from the given plaintext.
* This function should be used when the user does not know the specific type of the Asymmetric encryption he has,
* and therefore he is working on byte array.
* @param plaintext to generates byte array from. MUST be an instance of GroupElementPlaintext.
* @return the byte array generated from the given plaintext.
* @throws IllegalArgumentException if the given plaintext is not an instance of GroupElementPlaintext.
*/
vector<byte> ElGamalOnGroupElementEnc::generateBytesFromPlaintext(shared_ptr<Plaintext> plaintext) {
	
	auto plain = dynamic_pointer_cast<GroupElementPlaintext>(plaintext);
	if (plain == NULL) {
		throw invalid_argument("plaintext should be an instance of GroupElementPlaintext");
	}
	auto el = plain->getElement();
	return dlog->decodeGroupElementToByteArray(el.get());
}

/**
* Calculates the ciphertext resulting of multiplying two given ciphertexts.
* Both ciphertexts have to have been generated with the same public key and DlogGroup as the underlying objects of this ElGamal object.
* @throws IllegalStateException if no public key was set.
* @throws IllegalArgumentException in the following cases:
* 		1. If one or more of the given ciphertexts is not instance of ElGamalOnGroupElementCiphertext.
* 		2. If one or more of the GroupElements in the given ciphertexts is not a member of the underlying DlogGroup of this ElGamal encryption scheme.
*/
shared_ptr<AsymmetricCiphertext> ElGamalOnGroupElementEnc::multiply(shared_ptr<AsymmetricCiphertext> cipher1, shared_ptr<AsymmetricCiphertext> cipher2) {

	//Choose a random value in Zq.
	biginteger w = getRandomInRange(0, qMinusOne, random);

	//Call the other function that computes the multiplication.
	return multiply(cipher1, cipher2, w);
}

/**
* Calculates the ciphertext resulting of multiplying two given ciphertexts.<P>
* Both ciphertexts have to have been generated with the same public key and DlogGroup as the underlying objects of this ElGamal object.<p>
*
* There are cases when the random value is used after the function, for example, in sigma protocol.
* In these cases the random value should be known to the user. We decided not to have function that return it to the user
* since this can cause problems when the multiply function is called more than one time.
* Instead, we decided to have an additional multiply function that gets the random value from the user.
*
* @throws IllegalStateException if no public key was set.
* @throws IllegalArgumentException in the following cases:
* 		1. If one or more of the given ciphertexts is not instance of ElGamalOnGroupElementCiphertext.
* 		2. If one or more of the GroupElements in the given ciphertexts is not a member of the underlying DlogGroup of this ElGamal encryption scheme.
*/
shared_ptr<AsymmetricCiphertext> ElGamalOnGroupElementEnc::multiply(shared_ptr<AsymmetricCiphertext> cipher1, shared_ptr<AsymmetricCiphertext> cipher2, biginteger r) {
	/*
	* Pseudo-Code:
	* 	c1 = (u1, v1); c2 = (u2, v2)
	* 	COMPUTE u = g^w*u1*u2
	* 	COMPUTE v = h^w*v1*v2
	* 	OUTPUT c = (u,v)
	*/

	// If there is no public key can not encrypt, throws exception.
	if (!isKeySet()) {
		throw new IllegalStateException("in order to encrypt a message this object must be initialized with public key");
	}

	auto c1 = dynamic_pointer_cast<ElGamalOnGroupElementCiphertext>(cipher1);
	auto c2 = dynamic_pointer_cast<ElGamalOnGroupElementCiphertext>(cipher2);

	// Cipher1 and cipher2 should be ElGamal ciphertexts.
	if (c1 == NULL || c2 == NULL) {
		throw invalid_argument("ciphertexts should be instance of ElGamalCiphertext");
	}
	
	//Gets the groupElements of the ciphers.
	auto u1 = c1->getC1().get();
	auto v1 = c1->getC2().get();
	auto u2 = c2->getC1().get();
	auto v2 = c2->getC2().get();

	if (!(dlog->isMember(u1)) || !(dlog->isMember(v1)) || !(dlog->isMember(u2)) || !(dlog->isMember(v2))) {
		throw invalid_argument("GroupElements in the given ciphertexts must be a members in the DlogGroup of type " + dlog->getGroupType());
	}

	//Check that the r random value passed to this function is in Zq.
	if (!((r >= 0) && (r <=qMinusOne))) {
		throw invalid_argument("the given random value must be in Zq");
	}

	//Calculates u = g^w*u1*u2.
	auto gExpW = dlog->exponentiate(dlog->getGenerator().get(), r);
	auto gExpWmultU1 = dlog->multiplyGroupElements(gExpW.get(), u1);
	auto u = dlog->multiplyGroupElements(gExpWmultU1.get(), u2);

	//Calculates v = h^w*v1*v2.
	auto hExpW = dlog->exponentiate(publicKey->getH().get(), r);
	auto hExpWmultV1 = dlog->multiplyGroupElements(hExpW.get(), v1);
	auto v = dlog->multiplyGroupElements(hExpWmultV1.get(), v2);

	return make_shared<ElGamalOnGroupElementCiphertext>(u, v);
}

shared_ptr<AsymmetricCiphertext> ElGamalOnGroupElementEnc::reconstructCiphertext(shared_ptr<AsymmetricCiphertextSendableData> data) {
	auto data1 = dynamic_pointer_cast<ElGamalOnGrElSendableData>(data);
	if (data1 == NULL)
		throw invalid_argument("The input data has to be of type ElGamalOnGrElSendableData");
	
	auto cipher1 = dlog->reconstructElement(true, data1->getCipher1().get());
	auto cipher2 = dlog->reconstructElement(true, data1->getCipher2().get());
	return make_shared<ElGamalOnGroupElementCiphertext>(cipher1, cipher2);
}
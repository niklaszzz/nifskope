// Isolated x86 bridge for the user's PhysX 2.8.4 libraries. No game DLLs are
// distributed with NifSkope. Only explicit collision operations invoke it.
using System;
using System.IO;
using System.Diagnostics;
using System.Runtime.InteropServices;

internal static class PhysxCooker {
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32", CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module,string name);
    [DllImport("kernel32", CharSet=CharSet.Unicode)] static extern bool SetDllDirectory(string path);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] [return:MarshalAs(UnmanagedType.I1)] delegate bool Init(IntPtr allocator,IntPtr log);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Close();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] [return:MarshalAs(UnmanagedType.I1)] delegate bool Cook(ref MeshDesc mesh,IntPtr stream);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr CreateSdk(uint version,IntPtr allocator,IntPtr log,ref SdkDesc desc,out uint error);
    [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr CreateMesh(IntPtr sdk,IntPtr stream);
    [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate void ReleaseMesh(IntPtr sdk,IntPtr mesh);
    [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate void ReleaseSdk(IntPtr sdk);
    [UnmanagedFunctionPointer(CallingConvention.ThisCall)] [return:MarshalAs(UnmanagedType.I1)] delegate bool SaveMesh(IntPtr mesh,ref MeshDesc desc);
    [StructLayout(LayoutKind.Sequential,Pack=4)] struct SdkDesc {public uint pageSize,pageMax,convexMax,threadMask,flags;}
    [StructLayout(LayoutKind.Sequential,Pack=4)] struct MeshDesc {
        public uint vertices,triangles,pointStride,triangleStride;
        public IntPtr points,indices;
        public uint flags,materialStride;
        public IntPtr materials;
        public uint verticalAxis;
        public float verticalExtent;
        public IntPtr pmap;
        public float edgeThreshold;
    }
    static T Export<T>(IntPtr module,string name) where T:class {
        IntPtr address=GetProcAddress(module,name);
        if(address==IntPtr.Zero) throw new InvalidDataException("Missing PhysX export: "+name);
        return Marshal.GetDelegateForFunctionPointer(address,typeof(T)) as T;
    }
    static T Virtual<T>(IntPtr instance,int slot) where T:class {
        return Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(instance),slot*IntPtr.Size),typeof(T)) as T;
    }
    static void WithMesh(string directory,byte[] bytes, Action<MeshDesc> action) {
        IntPtr loader=LoadLibrary(Path.Combine(directory,"PhysXLoader.dll"));
        if(loader==IntPtr.Zero)throw new InvalidDataException("PhysXLoader.dll could not be loaded.");
        SdkDesc desc=new SdkDesc();desc.pageSize=65536;desc.pageMax=256;desc.convexMax=2048;desc.flags=1;
        uint error;IntPtr sdk=Export<CreateSdk>(loader,"NxCreatePhysicsSDK")(0x02080400,IntPtr.Zero,IntPtr.Zero,ref desc,out error);
        if(sdk==IntPtr.Zero)throw new InvalidDataException("PhysX SDK creation failed: "+error);
        try {
            using(var source=new NativeStream(bytes)) {
                IntPtr mesh=Virtual<CreateMesh>(sdk,8)(sdk,source.Pointer);
                if(mesh==IntPtr.Zero || source.Failed)throw new InvalidDataException("PhysX runtime rejected the collision mesh.");
                try {
                    MeshDesc result=new MeshDesc();
                    if(!Virtual<SaveMesh>(mesh,0)(mesh,ref result))throw new InvalidDataException("Cannot recover triangle mesh geometry.");
                    if(result.vertices==0 || result.vertices>4000000 || result.triangles==0 || result.triangles>4000000)
                        throw new InvalidDataException("Invalid collision mesh counts.");
                    action(result);
                } finally {Virtual<ReleaseMesh>(sdk,9)(sdk,mesh);}
            }
        } finally {Virtual<ReleaseSdk>(sdk,1)(sdk);}
    }
    static void Validate(string directory,byte[] bytes) {
        WithMesh(directory,bytes,d=>{
            uint nv=BitConverter.ToUInt32(bytes,28),nt=BitConverter.ToUInt32(bytes,32),flags=BitConverter.ToUInt32(bytes,12);
            if(nv!=d.vertices || nt!=d.triangles)throw new InvalidDataException("Cooked mesh counts do not match the runtime geometry.");
            int width=(flags&8)!=0?1:(flags&16)!=0?2:4;
            long end=36L+12L*nv+3L*nt*width;
            if(end>bytes.Length)throw new InvalidDataException("Incomplete cooked geometry.");
            // Verify the preview decoder against the actual game runtime.
            for(uint i=0;i<nv;++i)for(int a=0;a<3;++a) {
                float f=BitConverter.ToSingle(bytes,checked(36+(int)i*12+a*4));
                int bits=Marshal.ReadInt32(d.points,checked((int)(i*d.pointStride)+a*4));
                if(f!=BitConverter.ToSingle(BitConverter.GetBytes(bits),0))throw new InvalidDataException("Cooked vertex layout is unsupported.");
            }
            int runtimeWidth=(d.flags&2)!=0?2:4;
            for(uint i=0;i<nt;++i)for(int a=0;a<3;++a) {
                int offset=checked(36+(int)nv*12+((int)i*3+a)*width);
                uint index=width==1?bytes[offset]:width==2?BitConverter.ToUInt16(bytes,offset):BitConverter.ToUInt32(bytes,offset);
                int runtimeOffset=checked((int)(i*d.triangleStride)+a*runtimeWidth);
                uint actual=runtimeWidth==2?(ushort)Marshal.ReadInt16(d.indices,runtimeOffset):(uint)Marshal.ReadInt32(d.indices,runtimeOffset);
                if(index>=nv || index!=actual)throw new InvalidDataException("Cooked triangle layout is unsupported.");
            }
        });
    }
    static byte[] Rebuild(string directory,Cook cook,byte[] original,float[] points,uint[] triangles) {
        Validate(directory,original);
        byte[] cooked=null;
        WithMesh(directory,original,old=>{
            ushort[] materials=null;
            if(old.materials!=IntPtr.Zero) {
                ushort material=(ushort)Marshal.ReadInt16(old.materials);
                for(uint i=1;i<old.triangles;++i)
                    if((ushort)Marshal.ReadInt16(old.materials,checked((int)(i*old.materialStride)))!=material)
                        throw new InvalidDataException("Mixed per-face collision materials are not supported yet.");
                materials=new ushort[triangles.Length/3];for(int i=0;i<materials.Length;++i)materials[i]=material;
            }
            GCHandle p=GCHandle.Alloc(points,GCHandleType.Pinned),t=GCHandle.Alloc(triangles,GCHandleType.Pinned),m=new GCHandle();
            try {
                MeshDesc desc=old;desc.vertices=(uint)points.Length/3;desc.triangles=(uint)triangles.Length/3;
                desc.pointStride=12;desc.triangleStride=12;desc.points=p.AddrOfPinnedObject();desc.indices=t.AddrOfPinnedObject();
                desc.flags &= ~2u;desc.pmap=IntPtr.Zero;
                if(materials!=null){m=GCHandle.Alloc(materials,GCHandleType.Pinned);desc.materials=m.AddrOfPinnedObject();desc.materialStride=2;}
                // SaveToDesc does not retain every original cooking parameter.
                desc.edgeThreshold=BitConverter.ToSingle(original,16);
                desc.verticalAxis=BitConverter.ToUInt32(original,20);desc.verticalExtent=BitConverter.ToSingle(original,24);
                using(var output=new NativeStream(null)) {
                    if(!cook(ref desc,output.Pointer) || output.Failed)throw new InvalidDataException("PhysX rejected the edited triangle mesh.");
                    cooked=output.Bytes;
                }
            } finally {p.Free();t.Free();if(m.IsAllocated)m.Free();}
        });
        Validate(directory,cooked);return cooked;
    }
    static byte[] ReadBlob(BinaryReader reader) {
        int size=reader.ReadInt32();if(size<36 || size>134217728)throw new InvalidDataException("Invalid mesh size.");
        byte[] bytes=reader.ReadBytes(size);if(bytes.Length!=size)throw new EndOfStreamException();
        if(BitConverter.ToUInt32(bytes,0)!=0x0153584e || BitConverter.ToUInt32(bytes,4)!=0x4853454d || BitConverter.ToUInt32(bytes,8)!=1)
            throw new InvalidDataException("Unsupported cooked collision format.");
        return bytes;
    }
    sealed class NativeStream:IDisposable {
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr Destroy(IntPtr self,uint flags);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate byte ReadByte(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate ushort ReadWord(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate uint ReadDword(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate float ReadFloat(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate double ReadDouble(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate void ReadBuffer(IntPtr self,IntPtr dest,uint size);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr StoreByte(IntPtr self,byte value);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr StoreWord(IntPtr self,ushort value);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr StoreDword(IntPtr self,uint value);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr StoreFloat(IntPtr self,float value);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr StoreDouble(IntPtr self,double value);
        [UnmanagedFunctionPointer(CallingConvention.ThisCall)] delegate IntPtr StoreBuffer(IntPtr self,IntPtr source,uint size);
        public readonly IntPtr Pointer;
        readonly IntPtr table;
        readonly Delegate[] callbacks;
        readonly MemoryStream memory;
        readonly BinaryWriter writer;
        public bool Failed;
        byte[] Take(int size) {
            byte[] bytes=new byte[size];
            if(memory.Read(bytes,0,size)!=size) Failed=true;
            return bytes;
        }
        public NativeStream(byte[] input) {
            memory=input==null?new MemoryStream():new MemoryStream(input,false);
            writer=input==null?new BinaryWriter(memory):null;
            callbacks=new Delegate[] {
                new Destroy((s,f)=>s), new ReadByte(s=>Take(1)[0]),
                new ReadWord(s=>BitConverter.ToUInt16(Take(2),0)), new ReadDword(s=>BitConverter.ToUInt32(Take(4),0)),
                new ReadFloat(s=>BitConverter.ToSingle(Take(4),0)), new ReadDouble(s=>BitConverter.ToDouble(Take(8),0)),
                new ReadBuffer((s,d,n)=>{if(n>134217728){Failed=true;return;} byte[] bytes=Take((int)n);Marshal.Copy(bytes,0,d,bytes.Length);}),
                new StoreByte((s,v)=>{writer.Write(v);return s;}),new StoreWord((s,v)=>{writer.Write(v);return s;}),
                new StoreDword((s,v)=>{writer.Write(v);return s;}),new StoreFloat((s,v)=>{writer.Write(v);return s;}),
                new StoreDouble((s,v)=>{writer.Write(v);return s;}),
                new StoreBuffer((s,p,n)=>{if(n>134217728){Failed=true;return s;}byte[] bytes=new byte[(int)n];Marshal.Copy(p,bytes,0,bytes.Length);writer.Write(bytes);return s;})
            };
            table=Marshal.AllocHGlobal(callbacks.Length*IntPtr.Size);
            Pointer=Marshal.AllocHGlobal(IntPtr.Size);
            for(int i=0;i<callbacks.Length;++i) Marshal.WriteIntPtr(table,i*IntPtr.Size,Marshal.GetFunctionPointerForDelegate(callbacks[i]));
            Marshal.WriteIntPtr(Pointer,table);
        }
        public byte[] Bytes {get {return memory.ToArray();}}
        public void Dispose(){Marshal.FreeHGlobal(Pointer);Marshal.FreeHGlobal(table);memory.Dispose();GC.KeepAlive(callbacks);}
    }
    static int Main(string[] args) {
        try {
            bool batch=args.Length==4 && args[0]=="--cook";
            if(!batch && (args.Length!=3 || args[0]!="--self-test")) throw new ArgumentException("Usage: --cook PhysXCooking.dll input output");
            string dll=Path.GetFullPath(args[1]);
            var version=FileVersionInfo.GetVersionInfo(dll);
            if(version.FileMajorPart!=2 || version.FileMinorPart!=8 || version.FileBuildPart!=4)
                throw new InvalidDataException("PhysX 2.8.4 is required.");
            SetDllDirectory(Path.GetDirectoryName(dll));
            IntPtr module=LoadLibrary(dll);if(module==IntPtr.Zero)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            Init init=Export<Init>(module,"NxInitCooking");Close close=Export<Close>(module,"NxCloseCooking");Cook cook=Export<Cook>(module,"NxCookTriangleMesh");
            if(!init(IntPtr.Zero,IntPtr.Zero))throw new InvalidDataException("PhysX cooking initialization failed.");
            if(batch) {
                try {
                    using(var input=new BinaryReader(File.OpenRead(args[2])))
                    using(var memory=new MemoryStream())
                    using(var output=new BinaryWriter(memory)) {
                        if(input.ReadUInt32()!=0x31435850)throw new InvalidDataException("Invalid request version.");
                        int count=input.ReadInt32();if(count<1 || count>4096)throw new InvalidDataException("Invalid mesh count.");
                        output.Write(0x31435850u);output.Write(count);
                        for(int n=0;n<count;++n) {
                            byte[] original=ReadBlob(input);
                            int nv=input.ReadInt32(),nt=input.ReadInt32();
                            if(nv<3 || nv>4000000 || nt<1 || nt>4000000)throw new InvalidDataException("Invalid geometry counts.");
                            float[] vertices=new float[nv*3];uint[] faces=new uint[nt*3];
                            for(int i=0;i<vertices.Length;++i){vertices[i]=input.ReadSingle();if(float.IsNaN(vertices[i]) || float.IsInfinity(vertices[i]))throw new InvalidDataException("Non-finite vertex.");}
                            for(int i=0;i<faces.Length;++i){faces[i]=input.ReadUInt32();if(faces[i]>=nv)throw new InvalidDataException("Vertex index out of range.");}
                            byte[] bytes=Rebuild(Path.GetDirectoryName(dll),cook,original,vertices,faces);
                            output.Write(bytes.Length);output.Write(bytes);
                        }
                        if(input.BaseStream.Position!=input.BaseStream.Length)throw new InvalidDataException("Unexpected input data.");
                        output.Flush();File.WriteAllBytes(args[3],memory.ToArray());
                    }
                } finally {close();}
                return 0;
            }
            float[] points={0,0,0,1,0,0,0,1,0};int[] triangles={0,1,2};
            GCHandle p=GCHandle.Alloc(points,GCHandleType.Pinned), t=GCHandle.Alloc(triangles,GCHandleType.Pinned);
            try {
                MeshDesc desc=new MeshDesc();desc.vertices=3;desc.triangles=1;desc.pointStride=12;desc.triangleStride=12;
                desc.points=p.AddrOfPinnedObject();desc.indices=t.AddrOfPinnedObject();desc.verticalAxis=255;desc.edgeThreshold=.001f;
                using(var output=new NativeStream(null)) {
                    if(!cook(ref desc,output.Pointer) || output.Failed)throw new InvalidDataException("PhysX rejected the test triangle.");
                    Validate(Path.GetDirectoryName(dll),output.Bytes);
                    File.WriteAllBytes(args[2],output.Bytes);Console.WriteLine("Cooked "+output.Bytes.Length+" bytes using "+version.FileVersion);
                }
            } finally {p.Free();t.Free();close();}
            return 0;
        } catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}
    }
}
